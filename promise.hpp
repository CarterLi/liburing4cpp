#pragma once

#include <experimental/coroutine>
#include <variant>
#include <optional>

#include "cancelable.hpp"

/** An awaitable object that can be created directly (without calling an async function) */
template <typename T = void>
struct promise final: std::experimental::suspend_always, cancelable {
    promise() = default;
    /** Create a promise with cancellation support
     * @param cancel_fn a function that cancels this promise
     */
    template <typename CancelFn>
    promise(CancelFn&& cancel_fn): cancel_fn_(std::move(cancel_fn)) {}

    template <typename TPromise>
    void await_suspend(std::experimental::coroutine_handle<TPromise> caller) noexcept {
        on_suspended(&caller.promise().callee_);
        waiter_ = caller;
    }
    T await_resume() const {
        assert(result_.index() > 0);
        if (result_.index() == 2) {
            std::rethrow_exception(std::get<2>(result_));
        }
        on_resume();
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

    /** Attempt to cancel the operation bound in this promise
     * @throw (std::bad_function_call) If the operation doesn't support cancellation
     */
    void cancel() override {
        return cancel_fn_();
    }

private:
    std::experimental::coroutine_handle<> waiter_;
    std::variant<
        std::monostate,
        std::conditional_t<std::is_void_v<T>, std::monostate, T>,
        std::exception_ptr
    > result_;
    std::function<void ()> cancel_fn_;
};
