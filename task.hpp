#pragma once

// Original source:
// https://github.com/Quuxplusone/coro/blob/master/include/coro/gor_task.h

#include <exception>
#include <experimental/coroutine>
#include <variant>
#include <array>

#include "promise.hpp"

template <typename T = void>
struct task;

template <typename T>
struct task_promise_base {
    task<T> get_return_object();
    std::experimental::suspend_never initial_suspend() { return {}; }
    auto final_suspend() {
        struct Awaiter {
            task_promise_base *me_;
            bool await_ready() const noexcept { return false; }
            void await_suspend(std::experimental::coroutine_handle<> caller) const noexcept {
                if (me_->then) {
                    me_->then();
                } else if (me_->waiter_) {
                    me_->waiter_.resume();
                }
            }
            void await_resume() const noexcept {}
        };
        return Awaiter{this};
    }
    void unhandled_exception() {
        result_.template emplace<2>(std::current_exception());
    }

    std::function<void ()> then;

protected:
    friend class task<T>;
    task_promise_base() = default;
    std::experimental::coroutine_handle<> waiter_;
    std::variant<
        std::monostate,
        std::conditional_t<std::is_void_v<T>, std::monostate, T>,
        std::exception_ptr
    > result_;
};

template <typename T>
struct task_promise: task_promise_base<T> {
    using task_promise_base<T>::result_;

    template <typename U>
    void return_value(U&& u) {
        result_.template emplace<1>(static_cast<U&&>(u));
    }
};

template <>
struct task_promise<void>: task_promise_base<void> {
    void return_void() {
        result_.template emplace<1>(std::monostate {});
    }
};

template <typename T>
struct task {
    using promise_type = task_promise<T>;
    using handle_t = std::experimental::coroutine_handle<promise_type>;

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::experimental::coroutine_handle<> caller) noexcept {
        coro_.promise().waiter_ = caller;
    }

    T await_resume() const {
        auto& result_ = coro_.promise().result_;
        assert(result_.index() > 0);
        if (result_.index() == 2) {
            std::rethrow_exception(std::get<2>(result_));
        }
        if constexpr (!std::is_void_v<T>) {
            return std::get<1>(result_);
        }
    }

    T get_result() const {
        return await_resume();
    }

    bool done() const {
        return coro_.done();
    }

    task(): coro_(nullptr) {};

    task(const task&) = delete;

    task(task&& other) noexcept {
        coro_ = other.coro_;
        other.coro_ = nullptr;
    }

    task& operator =(const task&) = delete;

    task& operator =(task&& other) noexcept {
        if (coro_) coro_.destroy();
        coro_ = std::exchange(other.coro_, nullptr);
        return *this;
    }

    ~task() {
        if (!coro_) return;
        if (!coro_.done()) {
            coro_.promise().then = [coro_ = coro_] () mutable {
                // FIXME: Does this do right thing?
                if (coro_.promise().waiter_) {
                    coro_.promise().waiter_.destroy();
                }
                coro_.destroy();
            };
        } else {
            coro_.destroy();
        }
    }

    template <typename Fn>
    void then(Fn&& fn) {
        assert(coro_.promise().then || "Fn `then` has been attached");
        coro_.promise().then = std::move(fn);
    }

private:
    friend class task_promise_base<T>;
    task(promise_type *p) : coro_(handle_t::from_promise(*p)) {}
    handle_t coro_;
};

template <typename T>
task<T> task_promise_base<T>::get_return_object() {
    return task<T>(static_cast<task_promise<T> *>(this));
}
