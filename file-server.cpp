#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <chrono>

#include "fmt/format.h"
#include "io_service.hpp"

enum {
    BUF_SIZE = 1024,
};

using namespace std::literals;

// 一些预定义的错误返回体
static constexpr const auto http_404_hdr = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"sv;
static constexpr const auto http_400_hdr = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"sv;

// 解析到HTTP请求的文件后，发送本地文件系统中的文件
void http_send_file(io_service& coro, std::string filename, int clientfd, int dirfd) {
    if (filename == "./") filename = "./index.html";

    // 尝试打开待发送文件
    const auto infd = coro.await_openat(dirfd, filename.c_str(), O_RDONLY, 0);
    on_scope_exit closefd([=]() { close(infd); });

    if (struct statx st; infd < 0 || coro.await_statx(infd, "", AT_EMPTY_PATH, STATX_MODE | STATX_SIZE, &st) || !S_ISREG(st.stx_mode)) {
        // 文件未找到情况下发送404 error响应
        fmt::print("{}: file not found!\n", filename);
        coro.await_send(clientfd, http_404_hdr.data(), http_400_hdr.length(), MSG_NOSIGNAL);
    } else {
        auto contentType = [filename_view = std::string_view(filename)]() {
            auto extension = filename_view.substr(filename_view.find_last_of('.') + 1);
            return extension == "txt" ? "text/plain"sv : "application/octet-stream"sv;
        }();

        // 发送响应头
        auto resHead = fmt::format("HTTP/1.1 200 OK\r\nContent-type: {}\r\nContent-Length: {}\r\n\r\n", contentType, st.stx_size);
        coro.await_send(clientfd, resHead.data(), resHead.length(), MSG_NOSIGNAL | MSG_MORE);

        off_t offset = 0;
        std::array<char, BUF_SIZE> filebuf;
        for (; st.stx_size - offset > BUF_SIZE; offset += BUF_SIZE) {
            coro.await_read(infd, filebuf.data(), filebuf.size(), offset);
            coro.await_send(clientfd, filebuf.data(), filebuf.size(), MSG_NOSIGNAL | MSG_MORE);
        }
        if (st.stx_size > offset) {
            coro.await_read(infd, filebuf.data(), filebuf.size(), offset);
            coro.await_send(clientfd, filebuf.data(), st.stx_size - offset, MSG_NOSIGNAL);
        }
    }
}

// HTTP请求解析
void serve(io_service& coro, int clientfd, int dirfd) {
#ifndef NDEBUG
    coro.fiberName = "serve";
#endif
    fmt::print("Serving connection, sockfd {}; number of running coroutines: {}\n",
         clientfd, io_service::runningCoroutines);

    std::string_view buf_view;
    std::array<char, BUF_SIZE> buffer;
    int res = coro.await_recv(clientfd, buffer.data(), buffer.size(), MSG_NOSIGNAL);
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
        coro.await_send(clientfd, http_400_hdr.data(), http_400_hdr.size(), MSG_NOSIGNAL);
    }
}

void accept_connection(io_service& coro, int serverfd, int dirfd) {
#ifndef NDEBUG
    coro.fiberName = "accept_connection";
#endif
    while (int clientfd = coro.await_accept(serverfd, nullptr, nullptr, 0)) {
        if (clientfd < 0) panic("clientfd", -clientfd);

        // 新建新协程处理请求
        new io_service(
            [=](auto& coro) {
                serve(static_cast<io_service &>(coro), clientfd, dirfd);
            },
            [=, start = std::chrono::high_resolution_clock::now()] () {
                // 请求结束时清理资源
                shutdown(clientfd, SHUT_RDWR);
                close(clientfd);
                fmt::print("sockfd {} is closed, time used {}\n",
                    clientfd,
                    (std::chrono::high_resolution_clock::now() - start).count());
            }
        );
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fmt::print("Usage: {} <SERVER_PORT> <ROOT_DIR>\n", argv[0]);
        return 1;
    }

    int server_port = std::atoi(argv[1]);

    int dirfd = open(argv[2], O_DIRECTORY);
    if (dirfd < 0) panic("open dir");
    on_scope_exit closedir([=]() { close(dirfd); });

    // 初始化内核支持的原生异步IO操作实现
    if (io_uring_queue_init(32, &io_service::ring, 0)) panic("queue_init");
    on_scope_exit closerg([&]() { io_uring_queue_exit(&io_service::ring); });

    // 建立TCP套接字
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) panic("socket creation");
    on_scope_exit closesock([=]() {
        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);
    });

    // 设置允许端口重用
    if (int on = 1; setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) panic("SO_REUSEADDR");
    if (int on = 1; setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on))) panic("SO_REUSEPORT");

    // 绑定端口
    if (sockaddr_in addr = {
        .sin_family = AF_INET,
        // 这里要注意，端口号一定要使用htons先转化为网络字节序，否则绑定的实际端口可能和你需要的不同
        .sin_port = htons(server_port),
        .sin_addr = { INADDR_ANY },
        .sin_zero = {}, // 消除编译器警告
    }; bind(sockfd, reinterpret_cast<sockaddr *>(&addr), sizeof (sockaddr_in))) panic("socket binding");

    // 监听端口
    if (listen(sockfd, 128)) panic("listen");
    fmt::print("Listening: {}\n", server_port);

    (new io_service(
        [=](auto& coro) { accept_connection(static_cast<io_service &>(coro), sockfd, dirfd); }
    ))->run();
}
