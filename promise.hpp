#pragma once

#include <experimental/coroutine>
#include <variant>
#include <optional>

#include "task.hpp"

struct promise_base: std::experimental::suspend_always {
    promise_base() = default;
    template <typename CancelFn>
    promise_base(CancelFn&& fn): cancel(std::move(fn)) {}

    const std::function<void ()> cancel;
};

/** An awaitable object that can be created directly (without calling an async function) */
template <typename T = void>
struct promise: promise_base {
    using promise_base::promise_base;

    template <typename TT>
    void await_suspend(std::experimental::coroutine_handle<task_promise<TT>> caller) noexcept {
        caller.promise().callee_ = this;
        waiter_ = caller;
    }
    T await_resume() const {
        assert(result_.index() > 0);
        if (result_.index() == 2) {
            std::rethrow_exception(std::get<2>(result_));
        }
        if constexpr (!std::is_void_v<T>) {
            return std::get<1>(result_);
        }
    }

    /** Resolve the promise, and resume the coroutine */
    template <typename U, typename = std::enable_if_t<std::is_convertible_v<U, T>>>
    void resolve(U&& u) {
        result_.template emplace<1>(static_cast<U&&>(u));
        waiter_.resume();
    }
    void resolve() {
        static_assert (std::is_void_v<T>);
        result_.template emplace<1>(std::monostate{});
        waiter_.resume();
    }
    /** Reject the promise, and resume the coroutine */
    void reject(std::exception_ptr eptr) {
        result_.template emplace<2>(eptr);
        waiter_.resume();
    }

    /** Get whether the coroutine is done */
    bool done() const {
        return waiter_.done();
    }

private:
    std::experimental::coroutine_handle<> waiter_;
    std::variant<
        std::monostate,
        std::conditional_t<std::is_void_v<T>, std::monostate, T>,
        std::exception_ptr
    > result_;
};
