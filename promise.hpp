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
#include <variant>
#include <optional>

#include "cancelable.hpp"

/**
 * An awaitable object that can be created directly (without calling an async function)
 * @tparam T value type holded by this promise
 * @tparam nothrow if true, this promise cannot be rejected ( slightly better performance )
 **/
template <typename T = void, bool nothrow = false>
struct promise final: cancelable {
    promise() = default;
    /** Create a promise with cancellation support
     * @param cancel_fn a function that cancels this promise
     */
    template <typename CancelFn>
    promise(CancelFn&& cancel_fn, void* user_data = nullptr)
        : cancel_fn_(std::move(cancel_fn))
        , user_data_(user_data) {}

    bool await_ready() {
        return result_.index() > 0;
    }

    template <typename TPromise>
    void await_suspend(std::experimental::coroutine_handle<TPromise> caller) noexcept {
        on_suspend(&caller.promise().callee_);
        waiter_ = caller;
    }
    T await_resume() const {
        on_resume();
        if constexpr (!nothrow) {
            if (auto* pep = std::get_if<2>(&result_)) {
                std::rethrow_exception(*pep);
            }
        }
        if constexpr (!std::is_void_v<T>) {
            return *std::get_if<1>(&result_);
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
        if constexpr (!nothrow) {
            result_.template emplace<2>(eptr);
            waiter_.resume();
        } else {
            __builtin_unreachable();
        }
    }

    /** Get whether the coroutine is done */
    bool done() const {
        return waiter_.done();
    }

    /** Attempt to cancel the operation bound in this promise
     * @throw (std::bad_function_call) If the operation doesn't support cancellation
     */
    void cancel() override {
        if (!cancel_fn_) throw std::bad_function_call();
        return cancel_fn_(this, user_data_);
    }

private:
    std::experimental::coroutine_handle<> waiter_;
    std::variant<
        std::monostate,
        std::conditional_t<std::is_void_v<T>, std::monostate, T>,
        std::conditional_t<!nothrow, std::exception_ptr, std::monostate>
    > result_;
    void (*const cancel_fn_)(promise* self, void* user_data);
    void* const user_data_;
};
