#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <fmt/format.h> // https://github.com/fmtlib/fmt
#include <vector>
#include <numeric>

#include "io_service.hpp"

#define USE_FIXED_FILES_AND_BUFFERS 1
#define USE_POLL 0

enum {
    BUF_SIZE = 1024,
    MAX_CONN_SIZE = 128,
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
#   if USE_POLL
                    co_await service.poll(keyIdx, POLLIN, IOSQE_FIXED_FILE);
#   endif
                    int r = co_await service.read_fixed(keyIdx, pbuf, BUF_SIZE, 0, keyIdx, IOSQE_FIXED_FILE);
                    if (r <= 0) break;
                    co_await service.write_fixed(keyIdx, pbuf, r, 0, keyIdx, IOSQE_FIXED_FILE);
                }
                availKeys.push_back(keyIdx);
            } else {
#endif
                fmt::print("sockfd {} is accepted; number of running coroutines: {}\n",
                    clientfd, ++runningCoroutines);

                std::vector<char> buf(BUF_SIZE);
                iovec iov = { .iov_base = buf.data(), .iov_len = BUF_SIZE };
                msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1 };

                while (true) {
#   if USE_POLL
                    co_await service.poll(clientfd, POLLIN);
#   endif
                    int r = co_await service.recvmsg(clientfd, &msg, MSG_NOSIGNAL);
                    if (r <= 0) break;
                    iov.iov_len = r;
                    co_await service.sendmsg(clientfd, &msg, MSG_NOSIGNAL);
                    iov.iov_len = BUF_SIZE;
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

    auto work = accept_connection(service, sockfd);

    while (!work.done()) {
        auto [promise, res] = service.wait_event();
        promise->resolve(res);
    }

    work.get_result();
}
