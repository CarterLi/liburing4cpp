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
                // We won't call co_wait in main()
                // but await_suspend will only be called for co_wait
                // Therefore we have no waiter for tasks in main
                if (me_->waiter_) me_->waiter_.resume();
            }
            void await_resume() const noexcept {}
        };
        return Awaiter{this};
    }

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

template <class T>
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
            fmt::print(stderr, "WARNING: Coro {} is not done yet, detaching. May result in memory leak\n", coro_.address());
        } else {
            coro_.destroy();
        }
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

template <typename T, size_t N>
task<std::array<T, N>> when_all(std::array<task<T>, N> tasks) {
    std::array<T, N> result;
    std::array<task<>, N> tmp;
    size_t left = tasks.size();
    promise<std::array<T, N>> p;

    for (size_t i=0; i<tasks.size(); ++i) {
        tmp[i] = [&, i]() mutable -> task<> {
            try {
                result[i] = co_await tasks[i];
                if (!--left) p.resolve(std::move(result));
            } catch (...) {
                p.reject(std::current_exception());
            }
        }();
    }
    co_await p;
    co_return result;
}

template <typename T, size_t N>
task<T> when_any(std::array<task<T>, N> tasks) {
    std::array<task<>, N> tmp;
    size_t left = tasks.size();
    promise<T> p;

    for (size_t i=0; i<tasks.size(); ++i) {
        tmp[i] = [&, i]() mutable -> task<> {
            try {
                auto result = co_await tasks[i];
                if (!p.done()) p.resolve(std::move(result));
            } catch (...) {
                if (!--left) {
                    p.reject(
                        std::make_exception_ptr(
                            std::runtime_error("All tasks rejected")
                        )
                    );
                }
            }
        }();
    }
    co_return co_await p;
}

template <size_t N>
task<> when_all(std::array<task<>, N> tasks) {
    std::array<task<>, N> tmp;
    size_t left = tasks.size();
    promise<> p;

    for (size_t i=0; i<tasks.size(); ++i) {
        tmp[i] = [&, i]() mutable -> task<> {
            try {
                co_await tasks[i];
                if (!--left) p.resolve();
            } catch (...) {
                p.reject(std::current_exception());
            }
        }();
    }
    co_await p;
}

template <size_t N>
task<> when_any(std::array<task<>, N> tasks) {
    std::array<task<>, N> tmp;
    size_t left = tasks.size();
    promise<> p;

    for (size_t i=0; i<tasks.size(); ++i) {
        tmp[i] = [&, i]() mutable -> task<> {
            try {
                co_await tasks[i];
                if (!p.done()) p.resolve();
            } catch (...) {
                if (!--left) {
                    p.reject(
                        std::make_exception_ptr(
                            std::runtime_error("All tasks rejected")
                        )
                    );
                }
            }
        }();
    }
    co_await p;
}
