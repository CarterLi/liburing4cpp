#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <chrono>

#include "coroutine.hpp"

enum {
    SERVER_PORT = 8080,
    BUF_NUMBER = 4, // 经测至多12个，多了报 EFAULT
    BUF_SIZE = 1024,
};

using namespace std::literals;

// 一些预定义的错误返回体
static constexpr const auto http_404_hdr = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"sv;
static constexpr const auto http_400_hdr = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"sv;

// 解析到HTTP请求的文件后，发送本地文件系统中的文件
void http_send_file(Coroutine& coro, std::string filename, int clientfd, int dirfd) {
    if (filename == "./") filename = "./index.html";

    // 尝试打开待发送文件
    const auto infd = openat(dirfd, filename.c_str(), O_RDONLY
#if USE_LIBAIO
        | O_DIRECT // Try to make it truly async
#endif
    );
    on_scope_exit closefd([=]() { close(infd); });

    if (struct stat st; infd < 0 || fstat(infd, &st) || !S_ISREG(st.st_mode)) {
        // 文件未找到情况下发送404 error响应
        fmt::print("{}: file not found!\n", filename);
        coro.await_writev(clientfd, { to_iov(http_404_hdr) });
    } else {
        auto contentType = [filename_view = std::string_view(filename)]() {
            auto extension = filename_view.substr(filename_view.find_last_of('.') + 1);
            auto iter = MimeDicts.find(extension);
            if (iter == MimeDicts.end()) return "application/octet-stream"sv;
            return iter->second;
        }();

        // 发送响应头
        coro.await_writev(clientfd, {
            to_iov(fmt::format("HTTP/1.1 200 OK\r\nContent-type: {}\r\nContent-Length: {}\r\n\r\n", contentType, st.st_size)),
        });

        off_t offset = 0;
        std::array<char, BUF_SIZE> filebuf;
        auto iov = to_iov(filebuf);
        for (; st.st_size - offset > BUF_SIZE; offset += BUF_SIZE) {
            coro.await_readv(infd, { iov }, offset);
            coro.await_writev(clientfd, { iov });
            coro.delay(1); // For debugging
        }
        if (st.st_size > offset) {
            iov.iov_len = size_t(st.st_size - offset);
            coro.await_readv(infd, { iov }, offset);
            coro.await_writev(clientfd, { iov });
        }
    }
}

// HTTP请求解析
void serve(Coroutine& coro, int clientfd, int dirfd) {
    fmt::print("Serving connection, sockfd {}; number of running coroutines: {}\n",
         clientfd, Coroutine::runningCoroutines);

    std::string_view buf_view;
    std::array<char, BUF_SIZE> buffer;
    int res = coro.await_readv(clientfd, { to_iov(buffer) });
    buf_view = std::string_view(buffer.data(), size_t(res));

    // 这里我们只处理GET请求
    if (buf_view.compare(0, 3, "GET") == 0) {
        // 获取请求的path
        auto file = "."s += buf_view.substr(4, buf_view.find(' ', 4) - 4);
        fmt::print("received request {} with sockfd {}\n", file, clientfd);
        http_send_file(coro, file, clientfd, dirfd);
    } else {
        // 其他HTTP请求处理，如POST，HEAD等，返回400错误
        fmt::print("unsupported request: {}\n", buf_view);
        coro.await_writev(clientfd, { to_iov(http_400_hdr) });
    }
}

void accept_connection(Coroutine& coro, int serverfd, int dirfd) {
    while (coro.await_poll(serverfd)) {
        int clientfd = accept(serverfd, nullptr, nullptr);

        // 新建新协程处理请求
        new Coroutine(
            [=](auto& coro) {
                serve(static_cast<Coroutine &>(coro), clientfd, dirfd);
            },
            [=, start = std::chrono::high_resolution_clock::now()] () {
                // 请求结束时清理资源
                close(clientfd);
                fmt::print("sockfd {} is closed, time used {}\n",
                    clientfd,
                    (std::chrono::high_resolution_clock::now() - start).count());
            }
        );
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fmt::print("Usage: {} <ROOT_DIR>\n", argv[0]);
        return 1;
    }

    int dirfd = open(argv[1], O_DIRECTORY);
    if (dirfd < 0) panic("open dir");
    on_scope_exit closedir([=]() { close(dirfd); });

    // 初始化内核支持的原生异步IO操作实现
#if !USE_LIBAIO
    if (io_uring_queue_init(32, &ring, 0)) panic("queue_init");
    on_scope_exit closerg([&]() { io_uring_queue_exit(&ring); });
#else
    if (io_queue_init(32, &context)) panic("queue_init");
    on_scope_exit closerg([&]() { io_queue_release(context); });
#endif

    // 建立TCP套接字
    int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sockfd < 0) panic("socket creation");
    on_scope_exit closesock([=]() { close(sockfd); });

    // 设置允许端口重用
    if (int on = 1; setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) panic("SO_REUSEADDR");
    if (int on = 1; setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on))) panic("SO_REUSEPORT");

    // 绑定端口
    if (sockaddr_in addr = {
        AF_INET,
        // 这里要注意，端口号一定要使用htons先转化为网络字节序，否则绑定的实际端口可能和你需要的不同
        htons(SERVER_PORT),
        { INADDR_ANY },
        {}, // 消除编译器警告
    }; bind(sockfd, reinterpret_cast<sockaddr *>(&addr), sizeof (sockaddr_in))) panic("socket binding");

    // 监听端口
    if (listen(sockfd, 128)) panic("listen");
    fmt::print("Listening: {}\n", SERVER_PORT);

    new Coroutine(
        [=](auto& coro) { accept_connection(static_cast<Coroutine &>(coro), sockfd, dirfd); }
    );

    // 事件循环
    while (Coroutine::runningCoroutines) {
        // 获取已完成的IO事件
#if !USE_LIBAIO
        io_uring_cqe* cqe;
        if (io_uring_wait_cqe(&ring, &cqe)) panic("wait_cqe");
        auto* coro = static_cast<Coroutine *>(io_uring_cqe_get_data(cqe));
        auto res = cqe->res;
        io_uring_cqe_seen(&ring, cqe);
#else
        io_event event;
        io_getevents(context, 1, 1, &event, nullptr);
        auto* coro = static_cast<Coroutine *>(event.data);
        auto res = event.res;
#endif

        // 有已完成的事件，回到协程继续
        try {
            if (!coro->next(res)) delete coro;
        } catch (std::runtime_error& e) {
            fmt::print("{}\n", e.what());
            delete coro;
        }
    }
}
