#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <fmt/format.h> // https://github.com/fmtlib/fmt
#include <vector>
#include <numeric>

#include <liburing/io_service.hpp>

#define USE_SPLICE 0
#define USE_LINK 0
#define USE_POLL 0

enum {
    BUF_SIZE = 512,
    MAX_CONN_SIZE = 512,
};

int runningCoroutines = 0;

uio::task<> accept_connection(uio::io_service &service, int serverfd) {
    while (int clientfd = co_await service.accept(serverfd, nullptr, nullptr)) {
        [](uio::io_service &service, int clientfd) -> uio::task<> {
            __kernel_timespec timeout = { .tv_sec = 1, .tv_nsec = 0 };
            fmt::print("sockfd {} is accepted; number of running coroutines: {}\n",
                       clientfd, ++runningCoroutines);
            std::vector<char> buf(BUF_SIZE);
            while (true) {
                auto recvTask = service.read(clientfd, buf.data(), BUF_SIZE, 0, IOSQE_IO_LINK);
                service.link_timeout(&timeout);
                int r = co_await recvTask;
                if (r <= 0) {
                    if (r == -ETIMEDOUT) {
                        fmt::println("Read timed out! Closing!");
                        break;
                    }
                    fmt::println("Read failed! Error: {}", strerror(-r));
                    break;
                };
                co_await service.send(clientfd, buf.data(), r, MSG_NOSIGNAL);
            }
            service.shutdown(clientfd, SHUT_RDWR, IOSQE_IO_LINK);
            co_await service.close(clientfd);
            fmt::print("sockfd {} is closed; number of running coroutines: {}\n",
                       clientfd, --runningCoroutines);
        }(service, clientfd);
    }
}

int main(int argc, char *argv[]) {
    using uio::io_service;
    using uio::panic_on_err;
    using uio::on_scope_exit;
    using uio::panic;

    uint16_t server_port = 0;
    if (argc == 2) {
        server_port = (uint16_t) std::strtoul(argv[1], nullptr, 10);
    }
    if (server_port == 0) {
        fmt::print("Usage: {} <PORT>\n", argv[0]);
        return 1;
    }

    io_service service(MAX_CONN_SIZE);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0) | panic_on_err("socket creation", true);
    on_scope_exit closesock([=]() { shutdown(sockfd, SHUT_RDWR); });

    if (sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(server_port),
        .sin_addr = {INADDR_ANY},
        .sin_zero = {},
    }; bind(sockfd, reinterpret_cast<sockaddr *>(&addr), sizeof(sockaddr_in)))
        panic("socket binding", errno);

    if (listen(sockfd, MAX_CONN_SIZE * 2)) panic("listen", errno);
    fmt::print("Listening: {}\n", server_port);

    service.run(accept_connection(service, sockfd));
}
