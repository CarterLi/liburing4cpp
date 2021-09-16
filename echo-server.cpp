#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/fcntl.h>
#include <netinet/in.h>
#include <vector>
#include <numeric>
#include <chrono>

#include "fmt/format.h"
#include "io_coroutine.hpp"
#include "utils.hpp"

#define USE_SPLICE 1
#define USE_LINK 1
#define USE_POLL 1

enum {
    BUF_SIZE = 512,
    MAX_CONN_SIZE = 512,
};

int runningCoroutines = 0;

void serve(io_coroutine& service, int clientfd) {
    fmt::print("sockfd {} is accepted; number of running coroutines: {}\n", clientfd, ++runningCoroutines);
#if USE_SPLICE
    int pipefds[2];
    if (pipe(pipefds) < 0) panic("pipe");
#else
    std::vector<char> buf(BUF_SIZE);
#endif
    while (true) {
#if USE_POLL
#   if USE_LINK
        service.poll(clientfd, POLLIN, IOSQE_IO_LINK).detach();
#   else
        service.poll(clientfd, POLLIN).await();
#   endif
#endif
#if USE_SPLICE
#   if USE_LINK
        service.splice(clientfd, -1, pipefds[1], -1, -1, SPLICE_F_MOVE, IOSQE_IO_HARDLINK).detach();
        // SPLICE_F_NONBLOCK is required here because splicing from an empty pipe will block
        int r = service.splice(pipefds[0], -1, clientfd, -1, -1, SPLICE_F_MOVE | SPLICE_F_NONBLOCK).await();
        if (r <= 0) break;
#   else
        int r = service.splice(clientfd, -1, pipefds[1], -1, -1, SPLICE_F_MOVE).await();
        if (r <= 0) break;
        service.splice(pipefds[0], -1, clientfd, -1, -1, SPLICE_F_MOVE).await();
#   endif
#else
#   if USE_LINK
#       error This will not work because short read of IORING_OP_RECV is not considered an error
#   else
        int r = service.recv(clientfd, buf.data(), BUF_SIZE, MSG_NOSIGNAL).await();
        if (r <= 0) break;
        service.send(clientfd, buf.data(), r, MSG_NOSIGNAL).await();
#   endif
#endif
    }
}

void accept_connection(io_coroutine& coro, int serverfd) {
    while (int clientfd = coro.accept(serverfd, nullptr, nullptr, 0).await()) {
        new io_coroutine(
            coro.host,
            std::bind(serve, std::placeholders::_1, clientfd),
            [=, start = std::chrono::high_resolution_clock::now()] () {
                // 请求结束时清理资源
                shutdown(clientfd, SHUT_RDWR);
                close(clientfd);
                fmt::print("sockfd {} is closed, time used {}, running coroutines {}\n",
                    clientfd,
                    (std::chrono::high_resolution_clock::now() - start).count(),
                    --runningCoroutines);
            });
    }
}

int main(int argc, char *argv[]) {
    uint16_t server_port = 0;
    if (argc == 2) {
        server_port = (uint16_t)std::strtoul(argv[1], nullptr, 10);
    }
    if (server_port == 0) {
        fmt::print("Usage: {} <PORT>\n", argv[0]);
        return 1;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) panic("socket creation");
    on_scope_exit closesock([=]() {
        shutdown(sockfd, SHUT_RDWR);
        close(sockfd);
    });

    if (sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(server_port),
        .sin_addr = { INADDR_ANY },
        .sin_zero = {},
    }; bind(sockfd, reinterpret_cast<sockaddr *>(&addr), sizeof (sockaddr_in))) panic("socket binding");

    io_host host(MAX_CONN_SIZE);

    if (listen(sockfd, MAX_CONN_SIZE * 2)) panic("listen", errno);
    fmt::print("Listening: {}\n", server_port);

    new io_coroutine(
        host,
        std::bind(accept_connection, std::placeholders::_1, sockfd)
    );
    host.run();
}
