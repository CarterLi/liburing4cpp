#pragma once
#if __has_include(<coroutine>)
#   include <coroutine>
namespace std::experimental {
    using std::suspend_always;
    using std::suspend_never;
    using std::coroutine_handle;
}
#else
#   include <experimental/coroutine>
#endif
#include <liburing.h>

struct cqe_resolver {
    std::experimental::coroutine_handle<> handle;
    int result {};

    inline void resolve(int result) {
        this->result = result;
        handle.resume();
    }
};

struct sqe_awaitable {
    cqe_resolver resolver {};
    io_uring_sqe* sqe;

    // TODO: use cancel_token to implement cancellation
    sqe_awaitable(io_uring_sqe* sqe)
      : sqe(sqe) {}

    constexpr bool await_ready() const noexcept { return false; }

    void await_suspend(std::experimental::coroutine_handle<> handle) {
        resolver.handle = handle;
        io_uring_sqe_set_data(sqe, &resolver);
    }

    constexpr int await_resume() const noexcept { return resolver.result; }
};
