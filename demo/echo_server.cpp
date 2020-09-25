#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <fmt/format.h> // https://github.com/fmtlib/fmt
#include <vector>
#include <numeric>

#include "io_service.hpp"

#define USE_LINK 0
#define USE_POLL 0

enum {
    BUF_SIZE = 512,
    MAX_CONN_SIZE = 512,
};

int runningCoroutines = 0;

task<> accept_connection(io_service& service, int serverfd) {
    while (int clientfd = co_await service.accept(serverfd, nullptr, nullptr)) {
        [=, &service](int clientfd) -> task<> {
            fmt::print("sockfd {} is accepted; number of running coroutines: {}\n",
                clientfd, ++runningCoroutines);

            std::vector<char> buf(BUF_SIZE);
            while (true) {
#if USE_POLL
#   if USE_LINK
                service.poll(clientfd, POLLIN, IOSQE_IO_LINK);
#   else
                co_await service.poll(clientfd, POLLIN);
#   endif
#endif
                int r = co_await service.recv(clientfd, buf.data(), BUF_SIZE, MSG_NOSIGNAL);
                if (r <= 0) break;
                co_await service.send(clientfd, buf.data(), r, MSG_NOSIGNAL);
            }
            shutdown(clientfd, SHUT_RDWR);
            fmt::print("sockfd {} is closed; number of running coroutines: {}\n",
                clientfd, --runningCoroutines);
        }(clientfd);
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

    io_service service(MAX_CONN_SIZE);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0) | panic_on_err("socket creation", true);
    on_scope_exit closesock([=]() { shutdown(sockfd, SHUT_RDWR); });

    if (sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(server_port),
        .sin_addr = { INADDR_ANY },
        .sin_zero = {},
    }; bind(sockfd, reinterpret_cast<sockaddr *>(&addr), sizeof (sockaddr_in))) panic("socket binding", errno);

    if (listen(sockfd, MAX_CONN_SIZE * 2)) panic("listen", errno);
    fmt::print("Listening: {}\n", server_port);

    service.run(accept_connection(service, sockfd));
}
