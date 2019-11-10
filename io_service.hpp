#pragma once
#include <functional>
#include <system_error>
#include <sys/timerfd.h>
#include <unistd.h>
#include <liburing.h>   // http://git.kernel.dk/liburing
#include <sys/poll.h>
#include <fmt/format.h> // https://github.com/fmtlib/fmt

#include "task.hpp"

// 填充 iovec 结构体
constexpr inline iovec to_iov(void *buf, size_t size) noexcept {
    return { buf, size };
}
constexpr inline iovec to_iov(std::string_view sv) noexcept {
    return to_iov(const_cast<char *>(sv.data()), sv.size());
}
template <size_t N>
constexpr inline iovec to_iov(std::array<char, N>& array) noexcept {
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

class io_service {
public:
    io_service() {
        if (io_uring_queue_init(32, &ring, 0)) panic("queue_init");
    }
    ~io_service() {
        io_uring_queue_exit(&ring);
    }

    io_service(const io_service&) = delete;
    io_service& operator =(const io_service&) = delete;

public:
// 异步读操作，不使用缓冲区
#define DEFINE_AWAIT_OP(operation) \
    template <unsigned int N> \
    task<int> operation (int fd, iovec (&&ioves) [N], off_t offset = 0, uint32_t flags = 0, std::string_view command = #operation) { \
        auto* sqe = io_uring_get_sqe(&ring); \
        assert(sqe && "sqe should not be NULL"); \
        io_uring_prep_##operation (sqe, fd, ioves, N, offset); \
        sqe->flags = flags; \
        return await_work(command, sqe); \
    }
    DEFINE_AWAIT_OP(readv)
    DEFINE_AWAIT_OP(writev)
#undef DEFINE_AWAIT_OP

#define DEFINE_AWAIT_OP(operation) \
    template <unsigned int N> \
    task<int> operation(int sockfd, iovec (&&ioves) [N], uint32_t flags = 0, std::string_view command = #operation) { \
        msghdr msg = { \
            .msg_iov = ioves, \
            .msg_iovlen = N, \
        }; \
        auto* sqe = io_uring_get_sqe(&ring); \
        io_uring_prep_##operation(sqe, sockfd, &msg, flags); \
        return await_work(command, sqe); \
    }

    DEFINE_AWAIT_OP(recvmsg)
    DEFINE_AWAIT_OP(sendmsg)
#undef DEFINE_AWAIT_OP

    task<int> poll(int fd, std::string_view command = "poll") {
        auto* sqe = io_uring_get_sqe(&ring);
        assert(sqe && "sqe should not be NULL");
        io_uring_prep_poll_add(sqe, fd, POLLIN);
        return await_work(command, sqe);
    }

    task<int> yield(std::string_view command = "yield") {
        auto* sqe = io_uring_get_sqe(&ring);
        assert(sqe && "sqe should not be NULL");
        io_uring_prep_nop(sqe);
        return await_work(command, sqe);
    }

#if 0
    task<int> accept(int fd, sockaddr *addr, socklen_t *addrlen, int flags = 0, std::string_view command = "accept") {
        auto* sqe = io_uring_get_sqe(&ring);
        assert(sqe && "sqe should not be NULL");
        io_uring_prep_accept(sqe, fd, addr, addrlen, flags);
        return await_work(command, sqe);
    }

    task<int> delay(int second, std::string_view command = "delay") {
        auto* sqe = io_uring_get_sqe(&ring);
        assert(sqe && "sqe should not be NULL");
        __kernel_timespec exp = { second, 0 };
        io_uring_prep_timeout(sqe, &exp, 0, 0);
        return await_work(command, sqe);
    }
#else
    task<int> accept(int fd, sockaddr *addr, socklen_t *addrlen, int flags = 0, std::string_view command = "delay") {
        co_await poll(fd, command);
        co_return accept4(fd, addr, addrlen, flags);
    }

    task<int> delay(int second) {
        itimerspec exp = { {}, { second, 0 } };
        auto tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        if (timerfd_settime(tfd, 0, &exp, nullptr)) panic("timerfd");
        on_scope_exit closefd([=]() { close(tfd); });
        return poll(tfd, "delay");
    }
#endif

private:
    task<int> await_work(std::string_view command, io_uring_sqe* sqe) {
        promise<int> p;
        io_uring_sqe_set_data(sqe, &p);
        io_uring_submit(&ring);
        int res = co_await p;
        if (res < 0) panic(command, -res);
        co_return res;
    }

public:
    std::pair<promise<int> *, int> wait_event() {
        io_uring_cqe* cqe;
        promise<int> *coro;

        do {
            if (io_uring_wait_cqe(&ring, &cqe)) panic("wait_cqe");
            io_uring_cqe_seen(&ring, cqe);
            coro = static_cast<promise<int> *>(io_uring_cqe_get_data(cqe));
        } while (coro == nullptr);

        return { coro, cqe->res };
    }

public:
    std::optional<std::pair<promise<int> *, int>> peek_event() {
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
    std::optional<std::pair<promise<int> *, int>> timedwait_event(__kernel_timespec timeout) {
        if  (auto result = peek_event()) return result;

        io_uring_cqe* cqe;
        while (io_uring_wait_cqe_timeout(&ring, &cqe, &timeout) >= 0 && cqe) {
            io_uring_cqe_seen(&ring, cqe);

            if (auto* coro = static_cast<promise<int> *>(io_uring_cqe_get_data(cqe))) {
                return std::make_pair(coro, cqe->res);
            }
        }
        return std::nullopt;
    }

private:
    io_uring ring;
};
