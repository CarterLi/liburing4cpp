#pragma once
#include <functional>
#include <system_error>
#include <sys/timerfd.h>
#include <liburing.h>   // http://git.kernel.dk/liburing
#include <sys/poll.h>
#include <fmt/format.h> // https://github.com/fmtlib/fmt

#include "task.hpp"
#include "global.hpp"

// 填充 iovec 结构体
constexpr inline iovec to_iov(void *buf, size_t size) {
    return { buf, size };
}
constexpr inline iovec to_iov(std::string_view sv) {
    return to_iov(const_cast<char *>(sv.data()), sv.size());
}
template <size_t N>
constexpr inline iovec to_iov(std::array<char, N>& array) {
    return to_iov(array.data(), array.size());
}

template <typename Fn>
struct on_scope_exit {
    on_scope_exit(Fn &&fn): _fn(std::move(fn)) {}
    ~on_scope_exit() { this->_fn(); }

private:
    Fn _fn;
};

[[noreturn]]
void panic(std::string_view sv, int err = 0) { // 简单起见，把错误直接转化成异常抛出，终止程序
    if (err == 0) err = errno;
    fmt::print(stderr, "errno: {}\n", err);
    if (err == EPIPE) {
        throw std::runtime_error("Broken pipe: client socket is closed");
    }
    throw std::system_error(err, std::generic_category(), sv.data());
}


// 异步读操作，不使用缓冲区
#define DEFINE_AWAIT_OP(operation) \
template <unsigned int N> \
task<int> async_##operation (int fd, iovec (&&ioves) [N], off_t offset = 0) { \
    promise<int> p; \
    auto* sqe = io_uring_get_sqe(&ring); \
    assert(sqe && "sqe should not be NULL"); \
    io_uring_prep_##operation (sqe, fd, ioves, N, offset); \
    io_uring_sqe_set_data(sqe, &p); \
    io_uring_submit(&ring); \
    int res = co_await p; \
    if (res < 0) panic(#operation, -res); \
    co_return res; \
}
DEFINE_AWAIT_OP(readv)
DEFINE_AWAIT_OP(writev)
#undef DEFINE_AWAIT_OP


#define DEFINE_AWAIT_OP(operation) \
template <unsigned int N> \
task<int> async_##operation(int sockfd, iovec (&&ioves) [N], uint32_t flags) { \
    promise<int> p; \
    msghdr msg = { \
        .msg_iov = ioves, \
        .msg_iovlen = N, \
    }; \
    auto* sqe = io_uring_get_sqe(&ring); \
    io_uring_prep_##operation(sqe, sockfd, &msg, flags); \
    io_uring_sqe_set_data(sqe, &p); \
    io_uring_submit(&ring); \
    int res = co_await p; \
    if (res < 0) panic(#operation, -res); \
    co_return res; \
}

DEFINE_AWAIT_OP(recvmsg)
DEFINE_AWAIT_OP(sendmsg)
#undef DEFINE_AWAIT_OP

task<int> async_poll(int fd) {
    promise<int> p;
    auto* sqe = io_uring_get_sqe(&ring);
    assert(sqe && "sqe should not be NULL");
    io_uring_prep_poll_add(sqe, fd, POLLIN);
    io_uring_sqe_set_data(sqe, &p);
    io_uring_submit(&ring);
    int res = co_await p;
    if (res < 0) panic("poll", -res);
    co_return res;
}

task<int> async_delay(int second) {
    itimerspec exp = { {}, { second, 0 } };
    auto tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timerfd_settime(tfd, 0, &exp, nullptr)) panic("timerfd");
    on_scope_exit closefd([=]() { close(tfd); });
    return async_poll(tfd);
}

task<int> yield_execution() {
    promise<int> p;
    auto* sqe = io_uring_get_sqe(&ring);
    assert(sqe && "sqe should not be NULL");
    io_uring_prep_nop(sqe);
    io_uring_sqe_set_data(sqe, &p);
    io_uring_submit(&ring);
    co_return co_await p;
}

std::pair<promise<int> *, int> wait_for_event() {
    io_uring_cqe* cqe;
    promise<int> *coro;

    do {
        if (io_uring_wait_cqe(&ring, &cqe)) panic("wait_cqe");
        io_uring_cqe_seen(&ring, cqe);
        coro = static_cast<promise<int> *>(io_uring_cqe_get_data(cqe));
    } while (coro == nullptr);

    return { coro, cqe->res };
}

std::optional<std::pair<promise<int> *, int>> peek_a_event() {
    io_uring_cqe* cqe;
    while (io_uring_peek_cqe(&ring, &cqe) >= 0 && cqe) {
        io_uring_cqe_seen(&ring, cqe);

        if (auto* coro = static_cast<promise<int> *>(io_uring_cqe_get_data(cqe))) {
            return std::make_pair(coro, cqe->res);
        }
    }
    return std::nullopt;
}

// Requires Linux 5.4+
std::optional<std::pair<promise<int> *, int>> timedwait_for_event(__kernel_timespec timeout) {
    if  (auto result = peek_a_event()) return result;

    io_uring_cqe* cqe;
    while (io_uring_wait_cqe_timeout(&ring, &cqe, &timeout) >= 0 && cqe) {
        io_uring_cqe_seen(&ring, cqe);

        if (auto* coro = static_cast<promise<int> *>(io_uring_cqe_get_data(cqe))) {
            return std::make_pair(coro, cqe->res);
        }
    }
    return std::nullopt;
}
