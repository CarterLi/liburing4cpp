#pragma once

// Original source:
// https://github.com/Quuxplusone/coro/blob/master/include/coro/gor_task.h

#include <exception>
#include <experimental/coroutine>
#include <variant>
#include <array>
#include <algorithm>

template <class T = void>
struct task {
    struct promise_type {
        std::variant<std::monostate, T, std::exception_ptr> result_;
        std::experimental::coroutine_handle<void> waiter_;

        task get_return_object() { return task(this); }
        std::experimental::suspend_never initial_suspend() { return {}; }
        auto final_suspend() {
            struct Awaiter {
                promise_type *me_;
                bool await_ready() { return false; }
                void await_suspend(std::experimental::coroutine_handle<void> caller) {
                    // We won't call co_wait in main()
                    // but await_suspend will only be called for co_wait
                    // Therefore we have no waiter for tasks in main
                    if (me_->waiter_) me_->waiter_.resume();
                }
                void await_resume() {}
            };
            return Awaiter{this};
        }
        template<class U>
        void return_value(U&& u) {
            result_.template emplace<1>(static_cast<U&&>(u));
        }
        void unhandled_exception() {
            result_.template emplace<2>(std::current_exception());
        }
    };

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::experimental::coroutine_handle<void> caller) noexcept {
        coro_.promise().waiter_ = caller;
    }
    T await_resume() const {
        if (coro_.promise().result_.index() == 2) {
            std::rethrow_exception(std::get<2>(coro_.promise().result_));
        }
        return std::get<1>(coro_.promise().result_);
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
            fmt::print(stderr, "WARNING: Coro {} is not done yet, detaching. May result in memory leak\n", coro_.address());
        } else {
            coro_.destroy();
        }
    }

private:
    using handle_t = std::experimental::coroutine_handle<promise_type>;
    task(promise_type *p) : coro_(handle_t::from_promise(*p)) {}
    handle_t coro_;
};

template<>
struct task<void> {
    struct promise_type {
        std::optional<std::exception_ptr> result_;
        std::experimental::coroutine_handle<void> waiter_;

        task get_return_object() { return task(this); }
        std::experimental::suspend_never initial_suspend() { return {}; }
        auto final_suspend() {
            struct Awaiter {
                promise_type *me_;
                bool await_ready() { return false; }
                void await_suspend(std::experimental::coroutine_handle<void> caller) {
                    // We won't call co_wait in main()
                    // but await_suspend will only be called for co_wait
                    // Therefore we have no waiter for tasks in main
                    if (me_->waiter_) me_->waiter_.resume();
                }
                void await_resume() {}
            };
            return Awaiter{this};
        }
        void return_void() {}
        void unhandled_exception() {
            result_.template emplace(std::current_exception());
        }
    };

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::experimental::coroutine_handle<void> caller) noexcept {
        coro_.promise().waiter_ = caller;
    }
    void await_resume() const {
        if (coro_.promise().result_) {
            std::rethrow_exception(*coro_.promise().result_);
        }
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
    using handle_t = std::experimental::coroutine_handle<promise_type>;
    task(promise_type *p) : coro_(handle_t::from_promise(*p)) {}
    handle_t coro_;
};

template <typename T>
struct completion {
    bool await_ready() const noexcept {
        return false;
    }
    void await_suspend(std::experimental::coroutine_handle<> caller) noexcept {
        handle_ = caller;
    }
    T await_resume() const noexcept {
        if (result_.index() == 2) {
            std::rethrow_exception(std::get<2>(result_));
        }
        return std::get<1>(result_);
    }

    template<class U>
    void resolve(U&& u) {
        result_.template emplace<1>(static_cast<U&&>(u));
        handle_.resume();
    }
    void reject(std::exception_ptr eptr) {
        result_.template emplace<2>(eptr);
        handle_.resume();
    }

    bool done() const {
        return handle_.done();
    }

private:
    std::experimental::coroutine_handle<> handle_;
    std::variant<std::monostate, T, std::exception_ptr> result_;
};

template <typename T, size_t N>
task<std::array<T, N>> taskAll(std::array<task<T>, N> tasks) {
    std::array<T, N> result;
    std::array<task<>, N> tmp;
    size_t left = tasks.size();
    completion<std::array<T, N>> promise;

    for (size_t i=0; i<tasks.size(); ++i) {
        tmp[i] = [&, i]() mutable -> task<> {
            try {
                result[i] = co_await tasks[i];
                if (!--left)  promise.resolve(std::move(result));
            } catch (...) {
                promise.reject(std::current_exception());
            }
        }();
    }
    co_await promise;
    co_return result;
}

template <typename T, size_t N>
task<T> taskAny(std::array<task<T>, N> tasks) {
    std::array<task<>, N> tmp;
    size_t left = tasks.size();
    completion<T> promise;

    for (size_t i=0; i<tasks.size(); ++i) {
        tmp[i] = [&, i]() mutable -> task<> {
            try {
                auto result = co_await tasks[i];
                if (!promise.done()) promise.resolve(std::move(result));
            } catch (...) {
                if (!--left) {
                    promise.reject(
                        std::make_exception_ptr(
                            std::runtime_error("All tasks rejected")
                        )
                    );
                }
            }
        }();
    }
    co_return co_await promise;
}
