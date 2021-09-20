#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <vector>
#include <chrono>

#include "fmt/format.h"
#include "io_coroutine.hpp"
#include "utils.hpp"

#ifndef USE_SPLICE
#   define USE_SPLICE 0
#endif
#ifndef USE_LINK
#   define USE_LINK 0
#endif

enum {
    BUF_SIZE = 512,
};

void serve(io_coroutine& coro, int clientfd) noexcept {
    on_scope_exit closesock([=] {
        shutdown(clientfd, SHUT_RDWR);
        close(clientfd);
    });

#ifndef NDEBUG
    fmt::print("sockfd {} is accepted; number of running coroutines: {}\n", clientfd, coro.host.running_coroutines);
#endif
#if USE_SPLICE
    int pipefds[2];
    if (pipe(pipefds) < 0) panic("pipe");
#else
    std::vector<char> buf(BUF_SIZE);
#endif
    while (true) {
#if USE_SPLICE
#   if USE_LINK
        coro.splice(clientfd, -1, pipefds[1], -1, -1, SPLICE_F_MOVE, IOSQE_IO_HARDLINK).detach();
        // SPLICE_F_NONBLOCK is required here because splicing from an empty pipe will block
        int r = coro.splice(pipefds[0], -1, clientfd, -1, -1, SPLICE_F_MOVE | SPLICE_F_NONBLOCK).await();
        if (r <= 0) break;
#   else
        int r = coro.splice(clientfd, -1, pipefds[1], -1, -1, SPLICE_F_MOVE).await();
        if (r <= 0) break;
        coro.splice(pipefds[0], -1, clientfd, -1, r, SPLICE_F_MOVE).await();
#   endif
#else
        // RECV-SEND will not work with IOSQE_IO_LINK because short read of IORING_OP_RECV is not considered an error
        int r = coro.recv(clientfd, buf.data(), BUF_SIZE, MSG_NOSIGNAL).await();
        if (r <= 0) break;
        coro.send(clientfd, buf.data(), r, MSG_NOSIGNAL).await();
#endif
    }
}

void accept_connection(io_coroutine& coro, uint16_t server_port, int client_num) noexcept {
    int serverfd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverfd < 0) panic("socket creation (serverfd)");
    on_scope_exit closesock([=]() {
        shutdown(serverfd, SHUT_RDWR);
        close(serverfd);
    });

    if (sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(server_port),
        .sin_addr = { INADDR_ANY },
        .sin_zero = {},
    }; bind(serverfd, reinterpret_cast<sockaddr *>(&addr), sizeof (sockaddr_in))) panic("socket binding");

    if (listen(serverfd, client_num)) panic("listen", errno);

    for (int i = 0; i < client_num; ++i) {
        int clientfd = coro.accept(serverfd, nullptr, nullptr, 0).await();
        if (clientfd < 0) panic("accept");
        new io_coroutine(coro.host, std::bind(serve, std::placeholders::_1, clientfd));
    }
}

int64_t sum = 0;

void connect_server(io_coroutine& coro, uint16_t server_port, int msg_count) noexcept {
    int clientfd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientfd < 0) panic("socket creation (clientfd)");
    on_scope_exit closesock([&]() {
        shutdown(clientfd, SHUT_RDWR);
        close(clientfd);
    });

    if (sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(server_port),
        .sin_addr = { inet_addr("127.0.0.1") },
        .sin_zero = {},
    }; coro.connect(clientfd, reinterpret_cast<sockaddr *>(&addr), sizeof (sockaddr_in), 0).await() < 0) panic("connect");

    int num = 1;
    while (num <= msg_count) {
        int res = -1;
#if USE_LINK
        coro.send(clientfd, &num, sizeof num, MSG_NOSIGNAL, IOSQE_IO_LINK).detach();
#else
        coro.send(clientfd, &num, sizeof num, MSG_NOSIGNAL).await();
#endif
        [[maybe_unused]] int ret = coro.recv(clientfd, &res, sizeof res, MSG_NOSIGNAL).await();
        assert(ret == sizeof res);
        assert(res == num);
        sum += res;
        ++num;
    }
}

int main(int argc, char *argv[]) noexcept {
    uint16_t server_port = 0;
    int client_num = 0, msg_count = 0;
    if (argc == 4) {
        server_port = (uint16_t)std::strtoul(argv[1], nullptr, 10);
        client_num = (int)std::strtol(argv[2], nullptr, 10);
        msg_count = (int)std::strtol(argv[3], nullptr, 10);
    }
    if (server_port == 0 || client_num <= 0 || msg_count <= 0) {
        fmt::print("Usage: {} <PORT> <CLIENT_NUM> <MSG_COUNT>\n", argv[0]);
        return 1;
    }

    fmt::print("io_uring with {} and {}\n", USE_SPLICE ? "splice" : "recv/send", USE_LINK ? "link" : "no_link");
    auto start = std::chrono::high_resolution_clock::now();

    io_host host(client_num * 4);

    new io_coroutine(host, std::bind(accept_connection, std::placeholders::_1, server_port, client_num));

    for (int i = 0; i < client_num; ++i) {
        new io_coroutine(host, std::bind(connect_server, std::placeholders::_1, server_port, msg_count));
    }

    host.run();

    fmt::print("Finished in {}\n", (std::chrono::high_resolution_clock::now() - start).count());
    fmt::print("SUM: (1 + {}) * {} / 2 * {} = {}\n", msg_count, msg_count, client_num, sum);
}
