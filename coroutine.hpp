#pragma once
#include <functional>
#include <sys/timerfd.h>
#if !USE_LIBAIO
#   include <liburing.h>   // http://git.kernel.dk/liburing
#else
#   include <libaio.h>     // http://git.infradead.org/users/hch/libaio.git
#endif
#include <sys/poll.h>
#include <fmt/format.h> // https://github.com/fmtlib/fmt

#include "yield.hpp"
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

class Coroutine: public FiberSpace::Fiber<int, std::function<void ()>> {
public:
    using BaseType = FiberSpace::Fiber<int, std::function<void ()>>;
    using FuncType = BaseType::FuncType;

public:
    static int runningCoroutines;

    explicit Coroutine(FuncType f): BaseType(std::move(f)) {
        ++Coroutine::runningCoroutines;
        this->next(); // We have to make sure that this->yield() is executed at least once.
    }

    explicit Coroutine(FuncType f, std::function<void ()> cleanup): BaseType(std::move(f)) {
        this->localData = std::move(cleanup);
        ++Coroutine::runningCoroutines;
        this->next();
    }

    ~Coroutine() {
        --Coroutine::runningCoroutines;
        if (this->localData) this->localData();
    }

public:

// 异步读操作，不使用缓冲区
#if !USE_LIBAIO
#define DEFINE_AWAIT_OP(operation) \
template <unsigned int N> \
int await_##operation (int fd, iovec (&&ioves) [N], off_t offset = 0) { \
    auto* sqe = io_uring_get_sqe(&ring); \
    assert(sqe && "sqe should not be NULL"); \
    io_uring_prep_##operation (sqe, fd, ioves, N, offset); \
    io_uring_sqe_set_data(sqe, this); \
    io_uring_submit_and_wait(&ring, 1); \
    this->yield(); \
    int res = this->current().value(); \
    if (res < 0) panic(#operation, -res); \
    return res; \
}
#else
#define DEFINE_AWAIT_OP(operation) \
template <unsigned int N> \
int await_##operation (int fd, iovec (&&ioves) [N], off_t offset = 0) { \
    iocb ioq, *pioq = &ioq; \
    io_prep_p##operation(&ioq, fd, ioves, N, offset); \
    ioq.data = this; \
    io_submit(context, 1, &pioq); \
    this->yield(); \
    int res = this->current().value(); \
    if (res < 0) panic(#operation, -res); \
    return res; \
}
#endif
    DEFINE_AWAIT_OP(readv)
    DEFINE_AWAIT_OP(writev)
#undef DEFINE_AWAIT_OP

    int await_poll(int fd) {
#if !USE_LIBAIO
        auto* sqe = io_uring_get_sqe(&ring);
        assert(sqe && "sqe should not be NULL");
        io_uring_prep_poll_add(sqe, fd, POLLIN);
        io_uring_sqe_set_data(sqe, this);
        io_uring_submit_and_wait(&ring, 1);
#else
        iocb ioq, *pioq = &ioq;
        io_prep_poll(&ioq, fd, POLLIN);
        ioq.data = this;
        io_submit(context, 1, &pioq);
#endif
        this->yield();
        int res = this->current().value();
        if (res < 0) panic("poll", -res);
        return res;
    }

    // 把控制权交给其他协程
    int yield_execution() {
#if !USE_LIBAIO
        auto* sqe = io_uring_get_sqe(&ring);
        assert(sqe && "sqe should not be NULL");
        io_uring_prep_nop(sqe);
        io_uring_sqe_set_data(sqe, this);
        io_uring_submit_and_wait(&ring, 1);
#else
        iocb ioq, *pioq = &ioq;
        memset(&ioq, 0, sizeof(ioq));
        ioq.aio_lio_opcode = IO_CMD_NOOP;
        ioq.data = this;
        io_submit(context, 1, &pioq);
#endif
        this->yield();
        return this->current().value();
    }

    int delay(int second) {
        itimerspec exp = { {}, { second, 0 } };
        auto tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        if (timerfd_settime(tfd, 0, &exp, nullptr)) panic("timerfd");
        on_scope_exit closefd([=]() { close(tfd); });
        return this->await_poll(tfd);
    }
};
int Coroutine::runningCoroutines = 0;
