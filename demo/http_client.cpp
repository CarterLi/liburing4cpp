#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string>
#include <string_view>
#include <chrono>
#include <cerrno>
#include <fmt/format.h> // https://github.com/fmtlib/fmt

#include "io_service.hpp"

task<> start_work(io_service& service, const char* hostname) {
    addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
    }, *addrs;
    if (int ret = getaddrinfo(hostname, "http", &hints, &addrs); ret < 0) {
        fmt::print(stderr, "getaddrinfo({}): {}\n", hostname, gai_strerror(ret));
        throw std::runtime_error("getaddrinfo");
    }
    on_scope_exit freeaddr([=]() { freeaddrinfo(addrs); });

    for (struct addrinfo *addr = addrs; addr; addr = addr->ai_next) {
        int clientfd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
        if (clientfd < 0) panic("socket creation");
        on_scope_exit closesock([=]() { close(clientfd); });

        co_await service.connect(clientfd, addr->ai_addr, addr->ai_addrlen);

        co_await service.sendmsg(clientfd, { to_iov("GET / HTTP/1.0\r\n\r\n") }, MSG_NOSIGNAL);

        std::array<char, 1024> buffer;
        int res;
        do {
            res = co_await service.recvmsg(clientfd, { to_iov(buffer) }, MSG_NOSIGNAL);
            if (res == 0) break;
            co_await service.writev(STDOUT_FILENO, { to_iov(buffer.data(), size_t(res)) }, 0);
        } while (res == buffer.size());
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fmt::print("Usage: {} <URL>\n", argv[1]);
        return 1;
    }

    io_service service;

    // Start main coroutine ( for co_await )
    auto work = start_work(service, argv[1]);

    // Event loop
    while (!work.done()) {
        auto [promise, res] = service.wait_event();
        promise->resolve(res);
    }

    work.get_result();
}