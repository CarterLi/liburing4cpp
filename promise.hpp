#pragma once

#include <experimental/coroutine>
#include <variant>
#include <optional>

/** An awaitable object that can be created directly (without calling an async function) */
template <typename T = void>
struct promise {
    bool await_ready() const noexcept {
        return false;
    }
    void await_suspend(std::experimental::coroutine_handle<> caller) noexcept {
        handle_ = caller;
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
        handle_.resume();
    }
    void resolve() {
        static_assert (std::is_void_v<T>);
        result_.template emplace<1>(std::monostate{});
        handle_.resume();
    }
    /** Reject the promise, and resume the coroutine */
    void reject(std::exception_ptr eptr) {
        result_.template emplace<2>(eptr);
        handle_.resume();
    }

    /** Get is the coroutine done */
    bool done() const {
        return handle_.done();
    }

private:
    std::experimental::coroutine_handle<> handle_;
    std::variant<
        std::monostate,
        std::conditional_t<std::is_void_v<T>, std::monostate, T>,
        std::exception_ptr
    > result_;
};
