#pragma once

#include <climits>
#include <liburing.h>
#include <type_traits>
#include <optional>
#include <cassert>
#include <coroutine>

namespace uio {
struct resolver {
    virtual void resolve(int result) noexcept = 0;
};

struct resume_resolver final: resolver {
    friend struct sqe_awaitable;

    void resolve(int result) noexcept override {
        this->result = result;
        handle.resume();
    }

private:
    std::coroutine_handle<> handle;
    int result = 0;
};
static_assert(std::is_trivially_destructible_v<resume_resolver>);

struct deferred_resolver final: resolver {
    void resolve(int result) noexcept override {
        this->result = result;
    }

#ifndef NDEBUG
    ~deferred_resolver() {
        assert(!!result && "deferred_resolver is destructed before it's resolved");
    }
#endif

    std::optional<int> result;
};

struct callback_resolver final: resolver {
    callback_resolver(std::function<void (int result)>&& cb): cb(std::move(cb)) {}

    void resolve(int result) noexcept override {
        this->cb(result);
        delete this;
    }

private:
    std::function<void (int result)> cb;
};

struct sqe_awaitable {
    // TODO: use cancel_token to implement cancellation
    sqe_awaitable(io_uring_sqe* sqe) noexcept: sqe(sqe) {}

    // User MUST keep resolver alive before the operation is finished
    void set_deferred(deferred_resolver& resolver) {
        io_uring_sqe_set_data(sqe, &resolver);
    }

    void set_callback(std::function<void (int result)> cb) {
        io_uring_sqe_set_data(sqe, new callback_resolver(std::move(cb)));
    }

    auto operator co_await() {
        struct await_sqe {
            resume_resolver resolver {};
            io_uring_sqe* sqe;

            await_sqe(io_uring_sqe* sqe): sqe(sqe) {}

            constexpr bool await_ready() const noexcept { return false; }

            void await_suspend(std::coroutine_handle<> handle) noexcept {
                resolver.handle = handle;
                io_uring_sqe_set_data(sqe, &resolver);
            }

            constexpr int await_resume() const noexcept { return resolver.result; }
        };

        return await_sqe(sqe);
    }

private:
    io_uring_sqe* sqe;
};

} // namespace uio
