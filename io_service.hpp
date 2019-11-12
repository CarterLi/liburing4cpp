#pragma once
#include <functional>
#include <system_error>
#include <chrono>
#include <sys/timerfd.h>
#include <unistd.h>
#include <liburing.h>   // http://git.kernel.dk/liburing
#include <sys/poll.h>

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

constexpr inline __kernel_timespec dur2ts(std::chrono::nanoseconds dur) noexcept {
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(dur);
    dur -= secs;
    return { secs.count(), dur.count() };
}

[[noreturn]]
void panic(std::string_view sv, int err = 0) { // 简单起见，把错误直接转化成异常抛出，终止程序
    if (err == 0) err = errno;
    fprintf(stderr, "errno: %d\n", err);
    if (err == EPIPE) {
        throw std::runtime_error("Broken pipe: client socket is closed");
    }
    throw std::system_error(err, std::generic_category(), sv.data());
}

io_uring_sqe* io_uring_get_sqe_safe(io_uring *ring) noexcept {
    if (auto* sqe = io_uring_get_sqe(ring)) {
        return sqe;
    } else {
        io_uring_submit(ring);
        sqe = io_uring_get_sqe(ring);
        assert(sqe && "sqe should not be NULL");
        return sqe;
    }
}

// This cannot be an inlined function, or `stack-use-after-scope` happens
// forceinline won't work too
#define AWAIT_WORK(sqe, iflags, command) \
do { \
    promise<int> p; \
    sqe->flags = iflags; \
    io_uring_sqe_set_data(sqe, &p); \
    int res = co_await p; \
    if (res < 0) panic(command, -res); \
    co_return res; \
} while (false)

class io_service {
public:
    io_service(int entries = 64) {
        if (io_uring_queue_init(entries, &ring, 0)) panic("queue_init");
    }
    ~io_service() noexcept {
        io_uring_queue_exit(&ring);
    }

    io_service(const io_service&) = delete;
    io_service& operator =(const io_service&) = delete;

public:

#define DEFINE_AWAIT_OP(operation) \
    template <unsigned int N> \
    task<int> operation ( \
        int fd, \
        iovec (&&ioves) [N], \
        off_t offset, \
        uint8_t iflags = 0, \
        std::string_view command = #operation \
    ) { \
        auto* sqe = io_uring_get_sqe_safe(&ring); \
        io_uring_prep_##operation(sqe, fd, ioves, N, offset); \
        AWAIT_WORK(sqe, iflags, command); \
    }
    DEFINE_AWAIT_OP(readv)
    DEFINE_AWAIT_OP(writev)
#undef DEFINE_AWAIT_OP

#define DEFINE_AWAIT_OP(operation) \
    task<int> operation ( \
        int fd, \
        void* buf, \
        unsigned nbytes, \
        off_t offset, \
        int buf_index, \
        uint8_t iflags = 0, \
        std::string_view command = #operation \
    ) { \
        auto* sqe = io_uring_get_sqe_safe(&ring); \
        io_uring_prep_##operation(sqe, fd, buf, nbytes, offset, buf_index); \
        AWAIT_WORK(sqe, iflags, command); \
    }
    DEFINE_AWAIT_OP(read_fixed)
    DEFINE_AWAIT_OP(write_fixed)
#undef DEFINE_AWAIT_OP

#define DEFINE_AWAIT_OP(operation) \
    template <unsigned int N> \
    task<int> operation( \
        int sockfd, \
        iovec (&&ioves) [N], \
        uint32_t flags, \
        uint8_t iflags = 0, \
        std::string_view command = #operation \
    ) { \
        msghdr msg = { \
            .msg_iov = ioves, \
            .msg_iovlen = N, \
        }; \
        auto* sqe = io_uring_get_sqe_safe(&ring); \
        io_uring_prep_##operation(sqe, sockfd, &msg, flags); \
        AWAIT_WORK(sqe, iflags, command); \
    }

    DEFINE_AWAIT_OP(recvmsg)
    DEFINE_AWAIT_OP(sendmsg)
#undef DEFINE_AWAIT_OP

    task<int> poll(
        int fd,
        short poll_mask,
        uint8_t iflags = 0,
        std::string_view command = "poll"
    ) {
        auto* sqe = io_uring_get_sqe_safe(&ring);
        io_uring_prep_poll_add(sqe, fd, poll_mask);
        AWAIT_WORK(sqe, iflags, command);
    }

    task<int> yield(
        uint8_t iflags = 0,
        std::string_view command = "yield"
    ) {
        auto* sqe = io_uring_get_sqe_safe(&ring);
        io_uring_prep_nop(sqe);
        AWAIT_WORK(sqe, iflags, command);
    }

#if defined(USE_NEW_IO_URING_FEATURES)
    task<int> accept(
        int fd,
        sockaddr *addr,
        socklen_t *addrlen,
        int flags = 0,
        uint8_t iflags = 0,
        std::string_view command = "accept"
    ) {
        auto* sqe = io_uring_get_sqe_safe(&ring);
        io_uring_prep_accept(sqe, fd, addr, addrlen, flags);
        AWAIT_WORK(sqe, iflags, command);
    }

    task<int> delay(
        __kernel_timespec ts,
        uint8_t iflags = 0,
        std::string_view command = "delay"
    ) {
        auto* sqe = io_uring_get_sqe_safe(&ring);
        io_uring_prep_timeout(sqe, &ts, 0, 0);
        promise<int> p;
        sqe->flags = iflags;
        io_uring_sqe_set_data(sqe, &p);
        int res = co_await p;
        if (res < 0 && res != -ETIME) panic(command, -res);
        co_return res;
    }
#else
    task<int> accept(
        int fd,
        sockaddr *addr,
        socklen_t *addrlen,
        int flags = 0,
        uint8_t iflags = 0,
        std::string_view command = "accept"
    ) {
        co_await poll(fd, POLLIN, iflags, command);
        co_return accept4(fd, addr, addrlen, flags);
    }

    task<int> delay(
        __kernel_timespec ts,
        uint8_t iflags = 0, // IOSQE_IO_LINK doesn't work here since `timerfd_settime` is called before polling
        std::string_view command = "delay"
    ) {
        itimerspec exp = { {}, { ts.tv_sec, ts.tv_nsec } };
        auto tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        if (timerfd_settime(tfd, 0, &exp, nullptr)) panic("timerfd");
        on_scope_exit closefd([=]() { close(tfd); });
        // Cannot return poll directly or closefd will be called early incorrectly
        // which results in bad file discriptor exception
        co_await poll(tfd, POLLIN, iflags, command);
        printf("%d\n", (int)ts.tv_sec);
        co_return 0;
    }
#endif

    task<int> delay(
        std::chrono::nanoseconds dur,
        uint8_t iflags = 0,
        std::string_view command = "delay"
    ) {
        return delay(dur2ts(dur), iflags, command);
    }
#undef AWAIT_WORK

public:
    std::pair<promise<int> *, int> wait_event() {
        io_uring_cqe* cqe;
        promise<int>* coro;
        io_uring_submit(&ring);

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

    std::optional<std::pair<promise<int> *, int>> timedwait_event(std::chrono::nanoseconds dur) {
        return timedwait_event(dur2ts(dur));
    }

public:
    void register_files(std::initializer_list<int> fds) {
        if (io_uring_register_files(&ring, fds.begin(), fds.size()) < 0) panic("io_uring_register_files");
    }
    void unregister_files() {
        if (io_uring_unregister_files(&ring) < 0) panic("io_uring_unregister_files");
    }

public:
    template <unsigned int N>
    void register_buffers(iovec (&&ioves) [N]) {
        if (io_uring_register_buffers(&ring, &ioves[0], N)) panic("io_uring_register_buffers");
    }
    void unregister_buffers() {
        if (io_uring_unregister_buffers(&ring) < 0) panic("io_uring_unregister_buffers");
    }

public:
    io_uring& get_handle() {
        return ring;
    }

private:
    io_uring ring;
};
