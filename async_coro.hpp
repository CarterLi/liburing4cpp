#pragma once
#include <functional>
#include <system_error>
#include <sys/timerfd.h>
#if !USE_LIBAIO
#   include <liburing.h>   // http://git.kernel.dk/liburing
#else
#   include <libaio.h>     // http://git.infradead.org/users/hch/libaio.git
#endif
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

task<int> async_poll(int fd) {
    completion<int> promise;

#if !USE_LIBAIO
    auto* sqe = io_uring_get_sqe(&ring);
    assert(sqe && "sqe should not be NULL");
    io_uring_prep_poll_add(sqe, fd, POLLIN);
    io_uring_sqe_set_data(sqe, &promise);
    io_uring_submit(&ring);
#else
    iocb ioq, *pioq = &ioq;
    io_prep_poll(&ioq, fd, POLLIN);
    ioq.data = &promise;
    io_submit(context, 1, &pioq);
#endif

    int res = co_await promise;
    if (res < 0) panic("poll", -res);
    co_return res;
}

task<int> async_delay(int second) {
    itimerspec exp = { {}, { second, 0 } };
    auto tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timerfd_settime(tfd, 0, &exp, nullptr)) panic("timerfd");
    on_scope_exit closefd([=]() { close(tfd); });
    co_return co_await async_poll(tfd);
}

static std::pair<completion<int> *, int> wait_for_event() {
    // 获取已完成的IO事件
#if !USE_LIBAIO
    io_uring_cqe* cqe;
    completion<int> *coro;

    do {
        if (io_uring_wait_cqe(&ring, &cqe)) panic("wait_cqe");
        io_uring_cqe_seen(&ring, cqe);
        coro = static_cast<completion<int> *>(io_uring_cqe_get_data(cqe));
    } while (coro == nullptr);

    auto res = cqe->res;
#else
    io_event event;
    io_getevents(context, 1, 1, &event, nullptr);

    auto* coro = static_cast<completion<int> *>(event.data);
    auto res = event.res;
#endif
    return { coro, res };
}
