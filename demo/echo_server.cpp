#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <fmt/format.h> // https://github.com/fmtlib/fmt

#include "when.hpp"
#include "io_service.hpp"

enum {
    BUF_SIZE = 1024,
    FILES_SIZE = 512,
};

int runningCoroutines = 0;

task<> accept_connection(io_service& service, int serverfd) {
    while (int clientfd = co_await service.accept(serverfd, nullptr, nullptr)) {
        [=, &service](int clientfd) -> task<> {
            fmt::print("sockfd {} is accepted; number of running coroutines: {}\n",
                clientfd, ++runningCoroutines);
            int useFixedFile = clientfd < FILES_SIZE ? IOSQE_FIXED_FILE : 0;
            if (useFixedFile) {
                service.register_files_update(clientfd, &clientfd, 1);
            }

            char buf[BUF_SIZE];
            while (true) {
                auto tread = service.read(clientfd, buf, sizeof(buf), 0, IOSQE_IO_LINK | useFixedFile);
                auto twrite = service.write(clientfd, buf, sizeof(buf), 0, useFixedFile);
                if (co_await twrite < 0) {
                    int r = co_await tread;
                    if (r > 0) {
                        co_await service.write(clientfd, buf, r, 0, useFixedFile);
                    } else {
                        break;
                    }
                }
            }
            co_await service.close(clientfd);
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

    io_service service;

    std::array<int, FILES_SIZE> fds;
    std::fill(fds.begin(), fds.end(), -1);
    service.register_files(fds.data(), fds.size());

    int sockfd = socket(AF_INET, SOCK_STREAM, 0) | panic_on_err("socket creation", true);
    on_scope_exit closesock([=]() { close(sockfd); });

    if (sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(server_port),
        .sin_addr = { INADDR_ANY },
        .sin_zero = {},
    }; bind(sockfd, reinterpret_cast<sockaddr *>(&addr), sizeof (sockaddr_in))) panic("socket binding", errno);

    if (listen(sockfd, 128)) panic("listen", errno);
    fmt::print("Listening: {}\n", server_port);

    auto work = accept_connection(service, sockfd);

    while (!work.done()) {
        auto [promise, res] = service.wait_event();
        promise->resolve(res);
    }

    work.get_result();
}
