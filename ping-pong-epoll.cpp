#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <set>
#include <chrono>

#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "yield.hpp"
#include "utils.hpp"
#include "fmt/format.h"

#define USE_SPLICE 1

enum {
    BUF_SIZE = 512,
};

int running_coroutines = 0;

std::set<int> poll_fd_set;

static void await_poll(int epfd, int fd, uint32_t poll_mask, FiberSpace::Fiber<uint32_t, true>* coro) {
    struct epoll_event ev = {
        .events = poll_mask | EPOLLONESHOT | EPOLLET,
        .data = { .ptr = coro },
    };

    int op = poll_fd_set.emplace(fd).second ? EPOLL_CTL_ADD : EPOLL_CTL_MOD;
    if (epoll_ctl(epfd, op, fd, &ev) < 0) panic("epoll_ctl");
    coro->yield();
}

void serve(FiberSpace::Fiber<uint32_t, true>& coro, int epfd, int clientfd) noexcept {
    ++running_coroutines;
    on_scope_exit minusCount([] { --running_coroutines; });

    on_scope_exit closesock([=] {
        shutdown(clientfd, SHUT_RDWR);
        close(clientfd);
    });

#ifndef NDEBUG
    fmt::print("sockfd {} is accepted; number of running coroutines: {}\n", clientfd, running_coroutines);
#endif

#if USE_SPLICE
    int pipefds[2];
    if (pipe(pipefds) < 0) panic("pipe");
#else
    std::vector<char> buf(BUF_SIZE);
#endif

    while (true) {
        await_poll(epfd, clientfd, EPOLLIN, &coro);

#if USE_SPLICE
        int r = splice(clientfd, nullptr, pipefds[1], nullptr, -1, SPLICE_F_MOVE);
        if (r <= 0) break;
        await_poll(epfd, clientfd, EPOLLOUT, &coro);
        splice(pipefds[0], nullptr, clientfd, nullptr, r, SPLICE_F_MOVE);
#else
        int r = recv(clientfd, buf.data(), BUF_SIZE, MSG_NOSIGNAL);
        if (r <= 0) break;
        await_poll(epfd, clientfd, EPOLLOUT, &coro);
        send(clientfd, buf.data(), r, MSG_NOSIGNAL);
#endif
    }
}

void accept_connection(FiberSpace::Fiber<uint32_t, true>& coro, int epfd, uint16_t server_port, int client_num) noexcept {
    ++running_coroutines;
    on_scope_exit minusCount([] { --running_coroutines; });

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
        await_poll(epfd, serverfd, EPOLLIN, &coro);
        int clientfd = accept(serverfd, nullptr, nullptr);
        if (clientfd < 0) panic("accept");
        (new FiberSpace::Fiber<uint32_t, true>(std::bind(serve, std::placeholders::_1, epfd, clientfd)))->next();
    }
}

int64_t sum = 0;

void connect_server(FiberSpace::Fiber<uint32_t, true>& coro, int epfd, uint16_t server_port, int msg_count) noexcept {
    ++running_coroutines;
    on_scope_exit minusCount([] { --running_coroutines; });

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
    }; connect(clientfd, reinterpret_cast<sockaddr *>(&addr), sizeof (sockaddr_in)) < 0) panic("connect");

    int num = 1;
    while (num <= msg_count) {
        await_poll(epfd, clientfd, EPOLLOUT, &coro);
        send(clientfd, &num, sizeof num, MSG_NOSIGNAL);

        int res = -1;
        await_poll(epfd, clientfd, EPOLLIN, &coro);
        [[maybe_unused]] int ret = recv(clientfd, &res, sizeof res, MSG_NOSIGNAL);
        assert(ret == sizeof res);
        assert(res == num);
        sum += res;
        ++num;
    }
}

int main(int argc, char *argv[]) {
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

    fmt::print("epoll with {}\n", USE_SPLICE ? "splice" : "recv/send");
    auto start = std::chrono::high_resolution_clock::now();

    int epfd = epoll_create(client_num * 4);
    if (epfd < 0) panic("epoll_create");
    on_scope_exit close_epoll([=] () { close(epfd); });

    (new FiberSpace::Fiber<uint32_t, true>(std::bind(accept_connection, std::placeholders::_1, epfd, server_port, client_num)))->next();

    for (int i = 0; i < client_num; ++i) {
        (new FiberSpace::Fiber<uint32_t, true>(std::bind(connect_server, std::placeholders::_1, epfd, server_port, msg_count)))->next();
    }

    std::vector<epoll_event> events(client_num * 4);
    while (running_coroutines > 0) {
        int size = epoll_wait(epfd, events.data(), events.size(), -1);
        if (size < 0) panic("epoll_wait");
        for (int i = 0; i < size; ++i) {
            auto& event = events[i];
            auto* coro = (FiberSpace::Fiber<uint32_t, true>*)event.data.ptr;
            if (!coro->next((uint32_t)event.events)) delete coro;
        }
    }

    fmt::print("Finished in {}\n", (std::chrono::high_resolution_clock::now() - start).count());
    fmt::print("SUM: (1 + {}) * {} / 2 * {} = {}\n", msg_count, msg_count, client_num, sum);
}
