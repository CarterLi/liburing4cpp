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

#include <liburing/io_service.hpp>

uio::task<> start_work(uio::io_service& service, const char* hostname) {
    addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
    }, *addrs;
    if (int ret = getaddrinfo(hostname, "http", &hints, &addrs); ret < 0) {
        fmt::print(stderr, "getaddrinfo({}): {}\n", hostname, gai_strerror(ret));
        throw std::runtime_error("getaddrinfo");
    }
    uio::on_scope_exit freeaddr([=]() { freeaddrinfo(addrs); });

    for (struct addrinfo *addr = addrs; addr; addr = addr->ai_next) {
        int clientfd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol) | uio::panic_on_err("socket creation", true);
        uio::on_scope_exit closesock([&]() { service.close(clientfd); });

        if (co_await service.connect(clientfd, addr->ai_addr, addr->ai_addrlen) < 0) continue;

        auto header = fmt::format("GET / HTTP/1.0\r\nHost: {}\r\nAccept: */*\r\n\r\n", hostname);
        co_await service.send(clientfd, header.data(), header.size(), MSG_NOSIGNAL) | uio::panic_on_err("send", false);

        std::array<char, 1024> buffer;
        int res;
        for (;;) {
            res = co_await service.recv(clientfd, buffer.data(), buffer.size(), MSG_NOSIGNAL | MSG_MORE) | uio::panic_on_err("recv", false);
            if (res == 0) break;
            co_await service.write(STDOUT_FILENO, buffer.data(), unsigned(res), 0) | uio::panic_on_err("write", false);
        }

        co_return;
    }

    throw std::runtime_error("Unable to connect any resolved server");
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fmt::print("Usage: {} <URL>\n", argv[0]);
        return 1;
    }

    uio::io_service service;

    // Start main coroutine ( for co_await )
    service.run(start_work(service, argv[1]));
}
