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

struct task_promise_base {
    std::experimental::suspend_never initial_suspend() { return {}; }
    auto final_suspend() {
        struct Awaiter {
            task_promise_base *me_;
            bool await_ready() const noexcept { return false; }
            void await_suspend(std::experimental::coroutine_handle<void> caller) const noexcept {
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

    std::function<void ()> then;

protected:
    task_promise_base() = default;
    std::experimental::coroutine_handle<void> waiter_;
};

template <typename T>
struct task_promise: task_promise_base {
    task<T> get_return_object();
    template <typename U>
    void return_value(U&& u) {
        result_.template emplace<1>(static_cast<U&&>(u));
    }
    void unhandled_exception() {
        result_.template emplace<2>(std::current_exception());
    }

private:
    friend class task<T>;
    std::variant<std::monostate, T, std::exception_ptr> result_;
};

template <>
struct task_promise<void>: task_promise_base {
    task<> get_return_object();
    void return_void() {}
    void unhandled_exception() {
        result_.template emplace(std::current_exception());
    }

private:
    friend class task<>;
    std::optional<std::exception_ptr> result_;
};

template <typename T>
struct task {
    using promise_type = task_promise<T>;
    using handle_t = std::experimental::coroutine_handle<promise_type>;

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::experimental::coroutine_handle<void> caller) noexcept {
        coro_.promise().waiter_ = caller;
    }
    T await_resume() const;

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
    friend class task_promise<T>;
    task(promise_type *p) : coro_(handle_t::from_promise(*p)) {}
    handle_t coro_;
};

template <typename T>
T task<T>::await_resume() const {
    if (coro_.promise().result_.index() == 2) {
        std::rethrow_exception(std::get<2>(coro_.promise().result_));
    }
    return std::get<1>(coro_.promise().result_);
}

template <>
void task<>::await_resume() const {
    if (coro_.promise().result_) {
        std::rethrow_exception(*coro_.promise().result_);
    }
}

template <typename T>
task<T> task_promise<T>::get_return_object() { return task<T>(this); }

task<> task_promise<void>::get_return_object() { return task<>(this); }
