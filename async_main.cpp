#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <chrono>
#include <cerrno>

#include "global.hpp"
#include "async_coro.hpp"
#include "when.hpp"

enum {
    SERVER_PORT = 8080,
    BUF_SIZE = 1024,
};

using namespace std::literals;

// 一些预定义的错误返回体
static constexpr const auto http_404_hdr = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"sv;
static constexpr const auto http_400_hdr = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"sv;

int runningCoroutines = 0;

// 解析到HTTP请求的文件后，发送本地文件系统中的文件
task<> http_send_file(std::string filename, int clientfd, int dirfd) {
    if (filename == "./") filename = "./index.html";

    // 尝试打开待发送文件
    const auto infd = openat(dirfd, filename.c_str(), O_RDONLY);
    on_scope_exit closefd([=]() { close(infd); });

    if (struct stat st; infd < 0 || fstat(infd, &st) || !S_ISREG(st.st_mode)) {
        // 文件未找到情况下发送404 error响应
        fmt::print("{}: file not found!\n", filename);
        co_await async_sendmsg(clientfd, { to_iov(http_404_hdr) }, MSG_NOSIGNAL);
    } else {
        auto contentType = [filename_view = std::string_view(filename)]() {
            auto extension = filename_view.substr(filename_view.find_last_of('.') + 1);
            auto iter = MimeDicts.find(extension);
            if (iter == MimeDicts.end()) return "application/octet-stream"sv;
            return iter->second;
        }();

        // 发送响应头
        co_await async_sendmsg(clientfd, {
            to_iov(fmt::format("HTTP/1.1 200 OK\r\nContent-type: {}\r\nContent-Length: {}\r\n\r\n", contentType, st.st_size)),
        }, MSG_NOSIGNAL | MSG_MORE);

        off_t offset = 0;
        std::array<char, BUF_SIZE> filebuf;
        auto iov = to_iov(filebuf);
        for (; st.st_size - offset > BUF_SIZE; offset += BUF_SIZE) {
            co_await async_readv(infd, { iov }, offset);
            co_await async_sendmsg(clientfd, { iov }, MSG_NOSIGNAL | MSG_MORE);
            co_await async_delay(1); // For debugging
        }
        if (st.st_size > offset) {
            iov.iov_len = size_t(st.st_size - offset);
            co_await async_readv(infd, { iov }, offset);
            co_await async_sendmsg(clientfd, { iov }, MSG_NOSIGNAL);
        }
    }
}

// HTTP请求解析
task<> serve(int clientfd, int dirfd) {
    fmt::print("Serving connection, sockfd {}; number of running coroutines: {}\n",
         clientfd, runningCoroutines);

    std::string_view buf_view;
    std::array<char, BUF_SIZE> buffer;
    int res = co_await async_recvmsg(clientfd, { to_iov(buffer) }, MSG_NOSIGNAL);
    buf_view = std::string_view(buffer.data(), size_t(res));

    // 这里我们只处理GET请求
    if (buf_view.compare(0, 3, "GET") == 0) {
        // 获取请求的path
        auto file = "."s += buf_view.substr(4, buf_view.find(' ', 4) - 4);
        fmt::print("received request {} with sockfd {}\n", file, clientfd);
        co_await http_send_file(file, clientfd, dirfd);
    } else {
        // 其他HTTP请求处理，如POST，HEAD等，返回400错误
        fmt::print("unsupported request: {}\n", buf_view);
        co_await async_sendmsg(clientfd, { to_iov(http_400_hdr) }, MSG_NOSIGNAL);
    }
}

task<> accept_connection(int serverfd, int dirfd) {
    while (co_await async_poll(serverfd)) {
        int clientfd = accept(serverfd, nullptr, nullptr);

        // 新建新协程处理请求
        [=](int clientfd) -> task<> {
            ++runningCoroutines;
            auto start = std::chrono::high_resolution_clock::now();
            try {
                co_await serve(clientfd, dirfd);
            } catch (std::exception& e) {
                fmt::print("sockfd {} crashed with exception: {}\n",
                    clientfd,
                    e.what());
            }
            // 请求结束时清理资源
            close(clientfd);
            fmt::print("sockfd {} is closed, time used {}\n",
                clientfd,
                (std::chrono::high_resolution_clock::now() - start).count());
            --runningCoroutines;
        }(clientfd);
    }
}

int main_test() {
    if (io_uring_queue_init(32, &ring, 0)) panic("queue_init");
    on_scope_exit closerg([&]() { io_uring_queue_exit(&ring); });

    auto work = [] () -> task<> {
        co_await when_any(std::array {
            async_delay(1),
            async_delay(2),
            async_delay(3),
        });
        fmt::print("when_any\n");
        co_await when_all(std::array {
            async_delay(1),
            async_delay(2),
            async_delay(3),
        });
        fmt::print("when_all\n");
    }();

    // Event loop
    while (!work.done()) {
        auto [promise, res] = wait_for_event();

        // Found a finished event, go back to its coroutine.
        promise->resolve(res);
    }

    return 0;
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fmt::print("Usage: {} <ROOT_DIR>\n", argv[0]);
        return 1;
    }

    int dirfd = open(argv[1], O_DIRECTORY);
    if (dirfd < 0) panic("open dir");
    on_scope_exit closedir([=]() { close(dirfd); });

    if (io_uring_queue_init(32, &ring, 0)) panic("queue_init");
    on_scope_exit closerg([&]() { io_uring_queue_exit(&ring); });

    // 建立TCP套接字
    int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sockfd < 0) panic("socket creation");
    on_scope_exit closesock([=]() { close(sockfd); });

    // 设置允许端口重用
    if (int on = 1; setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) panic("SO_REUSEADDR");
    if (int on = 1; setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on))) panic("SO_REUSEPORT");

    // 绑定端口
    if (sockaddr_in addr = {
        .sin_family = AF_INET,
        // 这里要注意，端口号一定要使用htons先转化为网络字节序，否则绑定的实际端口可能和你需要的不同
        .sin_port = htons(SERVER_PORT),
        .sin_addr = { INADDR_ANY },
        .sin_zero = {}, // 消除编译器警告
    }; bind(sockfd, reinterpret_cast<sockaddr *>(&addr), sizeof (sockaddr_in))) panic("socket binding");

    // 监听端口
    if (listen(sockfd, 128)) panic("listen");
    fmt::print("Listening: {}\n", SERVER_PORT);

    auto work = accept_connection(sockfd, dirfd);

    // Event loop
    while (!work.done()) {
        auto [promise, res] = wait_for_event();

        // Found a finished event, go back to its coroutine.
        promise->resolve(res);
    }

    return 0;
}
