#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <fmt/format.h> // https://github.com/fmtlib/fmt
#include <vector>
#include <numeric>

#include "io_service.hpp"

#define USE_FIXED_FILES_AND_BUFFERS 0
#define USE_LINK 0
#define USE_POLL 0

enum {
    BUF_SIZE = 512,
    MAX_CONN_SIZE = 512,
};

int runningCoroutines = 0;

#if USE_FIXED_FILES_AND_BUFFERS
std::array<iovec, MAX_CONN_SIZE> fixedBuffers;
#endif

task<> accept_connection(io_service& service, int serverfd) {
#if USE_FIXED_FILES_AND_BUFFERS
    std::vector<int> availKeys(MAX_CONN_SIZE);
    std::iota(availKeys.rbegin(), availKeys.rend(), 0);
#endif

    while (int clientfd = co_await service.accept(serverfd, nullptr, nullptr)) {
        [=, &service
#if USE_FIXED_FILES_AND_BUFFERS
        , &availKeys
#endif
        ](int clientfd) -> task<> {
#if USE_FIXED_FILES_AND_BUFFERS
            if (__builtin_expect(!availKeys.empty(), true)) {
                const int keyIdx = availKeys.back();
                availKeys.pop_back();
                void* pbuf = fixedBuffers[keyIdx].iov_base;

                fmt::print("sockfd {} is accepted; use keyidx: {}; number of running coroutines: {}\n",
                    clientfd, keyIdx, ++runningCoroutines);
                service.register_files_update(keyIdx, &clientfd, 1);

                while (true) {
#   if USE_LINK
#       if USE_POLL
                    service.poll(keyIdx, POLLIN, IOSQE_FIXED_FILE | IOSQE_IO_LINK);
#       endif
                    auto tread = service.read_fixed(keyIdx, pbuf, BUF_SIZE, 0, keyIdx, IOSQE_IO_LINK | IOSQE_FIXED_FILE);
                    // If a short read is found, write_fixed will be canceled with -ECANCELED
                    int w = co_await service.write_fixed(keyIdx, pbuf, BUF_SIZE, 0, keyIdx, IOSQE_FIXED_FILE);
                    if (w < 0) {
                        int r = tread.get_result();
                        if (r <= 0) break;
                        co_await service.write_fixed(keyIdx, pbuf, r, 0, keyIdx, IOSQE_FIXED_FILE);
                    }
#   else
#       if USE_POLL
                    co_await service.poll(keyIdx, POLLIN, IOSQE_FIXED_FILE);
#       endif
                    int r = co_await service.read_fixed(keyIdx, pbuf, BUF_SIZE, 0, keyIdx, IOSQE_FIXED_FILE);
                    if (r <= 0) break;
                    co_await service.write_fixed(keyIdx, pbuf, r, 0, keyIdx, IOSQE_FIXED_FILE);
#   endif
                }
                availKeys.push_back(keyIdx);
            } else {
#endif
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
#if USE_FIXED_FILES_AND_BUFFERS
            }
#endif
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

#if USE_FIXED_FILES_AND_BUFFERS
    std::array<int, MAX_CONN_SIZE> fds;
    std::fill(fds.begin(), fds.end(), -1);
    service.register_files(fds.data(), fds.size());

    for (auto& iov : fixedBuffers) {
        iov.iov_base = new char[BUF_SIZE];
        iov.iov_len = BUF_SIZE;
    }
    service.register_buffers(fixedBuffers.data(), fixedBuffers.size());
#endif

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
