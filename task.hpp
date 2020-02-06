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
#include <exception>
#include <variant>
#include <array>
#include <cassert>

#include "cancelable.hpp"

// only for internal usage
template <typename T, bool nothrow>
struct task_promise_base: cancelable_promise_base {
    task<T, nothrow> get_return_object();
    auto initial_suspend() { return std::experimental::suspend_never(); }
    auto final_suspend() noexcept {
        struct Awaiter: std::experimental::suspend_always {
            task_promise_base *me_;

            Awaiter(task_promise_base *me): me_(me) {};
            void await_suspend(std::experimental::coroutine_handle<> caller) const noexcept {
                if (__builtin_expect(me_->result_.index() == 3, false)) {
                    if (me_->waiter_) {
                        me_->waiter_.destroy();
                    }
                    std::experimental::coroutine_handle<task_promise_base>::from_promise(*me_).destroy();
                } else if (me_->waiter_) {
                    me_->waiter_.resume();
                }
            }
        };
        return Awaiter(this);
    }
    void unhandled_exception() {
        if constexpr (!nothrow) {
            if (__builtin_expect(result_.index() == 3, false)) return;
            result_.template emplace<2>(std::current_exception());
        } else {
            __builtin_unreachable();
        }
    }

protected:
    friend struct task<T, nothrow>;
    task_promise_base() = default;
    std::experimental::coroutine_handle<> waiter_;
    std::variant<
        std::monostate,
        std::conditional_t<std::is_void_v<T>, std::monostate, T>,
        std::conditional_t<!nothrow, std::exception_ptr, std::monostate>,
        std::monostate // indicates that the promise is detached
    > result_;
};

// only for internal usage
template <typename T, bool nothrow>
struct task_promise final: task_promise_base<T, nothrow> {
    using task_promise_base<T, nothrow>::result_;

    template <typename U>
    void return_value(U&& u) {
        if (__builtin_expect(result_.index() == 3, false)) return;
        result_.template emplace<1>(static_cast<U&&>(u));
    }
    void return_value(int u) {
        if (__builtin_expect(result_.index() == 3, false)) return;
        result_.template emplace<1>(u);
    }
};

template <bool nothrow>
struct task_promise<void, nothrow> final: task_promise_base<void, nothrow> {
    using task_promise_base<void, nothrow>::result_;

    void return_void() {
        if (__builtin_expect(result_.index() == 3, false)) return;
        result_.template emplace<1>(std::monostate {});
    }
};

/**
 * An awaitable object that returned by an async function
 * @tparam T value type holded by this task
 * @tparam nothrow if true, the coroutine assigned by this task won't throw exceptions ( slightly better performance )
 * @warning do NOT discard this object when returned by some function, or UB WILL happen
 */
template <typename T = void, bool nothrow = false>
struct task final: cancelable {
    using promise_type = task_promise<T, nothrow>;
    using handle_t = std::experimental::coroutine_handle<promise_type>;

    task(const task&) = delete;
    task& operator =(const task&) = delete;

    bool await_ready() {
        auto& result_ = coro_.promise().result_;
        return result_.index() > 0;
    }

    template <typename T_, bool nothrow_>
    void await_suspend(std::experimental::coroutine_handle<task_promise<T_, nothrow_>> caller) noexcept {
        on_suspend(&caller.promise().callee_);
        coro_.promise().waiter_ = caller;
    }

    T await_resume() const {
        on_resume();
        return get_result();
    }

    /** Get the result hold by this task */
    T get_result() const {
        auto& result_ = coro_.promise().result_;
        assert(result_.index() != 0);
        if constexpr (!nothrow) {
            if (auto* pep = std::get_if<2>(&result_)) {
                std::rethrow_exception(*pep);
            }
        }
        if constexpr (!std::is_void_v<T>) {
            return *std::get_if<1>(&result_);
        }
    }

    /** Get is the coroutine done */
    bool done() const {
        return coro_.done();
    }

    /** Only for placeholder */
    task(): coro_(nullptr) {};

    task(task&& other) noexcept {
        coro_ = other.coro_;
        other.coro_ = nullptr;
    }

    task& operator =(task&& other) noexcept {
        if (coro_) coro_.destroy();
        coro_ = std::exchange(other.coro_, nullptr);
        return *this;
    }

    /** Destroy (when done) or detach (when not done) the task object */
    ~task() {
        if (!coro_) return;
        if (!coro_.done()) {
            coro_.promise().result_.template emplace<3>(std::monostate{});
        } else {
            coro_.destroy();
        }
    }

    /** Attempt to cancel the operation bound in this task
     * @note task itself doesn't support cancellation, the request is forwarded to inner promise.
     * @return true if cancelation is supported; false otherwise
     */
    bool cancel() override {
        assert(coro_.promise().callee_);
        return coro_.promise().callee_->cancel();
    }

private:
    friend struct task_promise_base<T, nothrow>;
    task(promise_type *p): coro_(handle_t::from_promise(*p)) {}
    handle_t coro_;
};

template <typename T, bool nothrow>
task<T, nothrow> task_promise_base<T, nothrow>::get_return_object() {
    return task<T, nothrow>(static_cast<task_promise<T, nothrow> *>(this));
}
