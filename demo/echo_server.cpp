#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <fmt/format.h> // https://github.com/fmtlib/fmt
#include <vector>
#include <numeric>

#include "when.hpp"
#include "io_service.hpp"

enum {
    BUF_SIZE = 512,
    MAX_CONN_SIZE = 64,
};

int runningCoroutines = 0;
std::array<iovec, MAX_CONN_SIZE> fixedBuffers;

task<> accept_connection(io_service& service, int serverfd) {
    std::vector<int> availKeys(MAX_CONN_SIZE);
    std::iota(availKeys.rbegin(), availKeys.rend(), 0);

    while (int clientfd = co_await service.accept(serverfd, nullptr, nullptr)) {
        [=, &service, &availKeys](int clientfd) -> task<> {
            const int keyIdx = availKeys.back();
            availKeys.pop_back();
            void* pbuf = fixedBuffers[keyIdx].iov_base;

            fmt::print("sockfd {} is accepted; use keyidx: {}; number of running coroutines: {}\n",
                clientfd, keyIdx, ++runningCoroutines);
            service.register_files_update(keyIdx, &clientfd, 1);

            while (true) {
#if 1
                int r = co_await service.read_fixed(keyIdx, pbuf, BUF_SIZE, 0, keyIdx, IOSQE_FIXED_FILE);
                if (r <= 0) break;
                co_await service.write_fixed(keyIdx, pbuf, r, 0, keyIdx, IOSQE_FIXED_FILE);
#else
                // Following code is about 30% slower then the code above
                // See https://github.com/axboe/liburing/issues/67
                auto tread = service.read_fixed(keyIdx, pbuf, BUF_SIZE, 0, keyIdx, IOSQE_IO_LINK | IOSQE_FIXED_FILE);
                // If a short read is found, write_fixed will be canceled with -ECANCELED
                int w = co_await service.write_fixed(keyIdx, pbuf, BUF_SIZE, 0, keyIdx, IOSQE_FIXED_FILE);
                if (w < 0) {
                    int r = tread.get_result();
                    if (r > 0) {
                        co_await service.write_fixed(keyIdx, pbuf, r, 0, keyIdx, IOSQE_FIXED_FILE);
                    } else {
                        break;
                    }
                }
#endif
            }
            close(clientfd);
            fmt::print("sockfd {} is closed; number of running coroutines: {}\n",
                clientfd, --runningCoroutines);
            availKeys.push_back(keyIdx);
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

    std::array<int, MAX_CONN_SIZE> fds;
    std::fill(fds.begin(), fds.end(), -1);
    service.register_files(fds.data(), fds.size());

    for (auto& iov : fixedBuffers) {
        iov.iov_base = new char[BUF_SIZE];
        iov.iov_len = BUF_SIZE;
    }
    service.register_buffers(fixedBuffers.data(), fixedBuffers.size());

    int sockfd = socket(AF_INET, SOCK_STREAM, 0) | panic_on_err("socket creation", true);
    on_scope_exit closesock([=]() { close(sockfd); });

    if (sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(server_port),
        .sin_addr = { INADDR_ANY },
        .sin_zero = {},
    }; bind(sockfd, reinterpret_cast<sockaddr *>(&addr), sizeof (sockaddr_in))) panic("socket binding", errno);

    if (listen(sockfd, MAX_CONN_SIZE)) panic("listen", errno);
    fmt::print("Listening: {}\n", server_port);

    auto work = accept_connection(service, sockfd);

    while (!work.done()) {
        auto [promise, res] = service.wait_event();
        promise->resolve(res);
    }

    work.get_result();
}
