#pragma once

// Original source:
// https://www.youtube.com/watch?v=8C8NnE1Dg4A&t=45m50s (Gor Nishanov, CppCon 2016)

#include <exception>
#include <experimental/coroutine>
#include <variant>
#include <span>

template<class T>
struct task {
    struct promise_type {
        std::variant<std::monostate, T, std::exception_ptr> result_;
        std::experimental::coroutine_handle<void> waiter_;

        task get_return_object() { return task(this); }
        auto initial_suspend() { return std::experimental::suspend_always{}; }
        auto final_suspend() {
            struct Awaiter {
                promise_type *me_;
                bool await_ready() { return false; }
                void await_suspend(std::experimental::coroutine_handle<void> caller) {
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

    bool await_ready() { return false; }
    void await_suspend(std::experimental::coroutine_handle<void> caller) {
        coro_.promise().waiter_ = caller;
        coro_.resume();
    }
    T await_resume() {
        if (coro_.promise().result_.index() == 2) {
            std::rethrow_exception(std::get<2>(coro_.promise().result_));
        }
        return std::get<1>(coro_.promise().result_);
    }

    T get_result() {
        return std::get<T>(coro_.promise().result_);
    }

    void start() {
        await_suspend(nullptr);
    }

    bool done() const {
        return coro_.done();
    }

    task(const task&) = delete;

    task(task&& other) noexcept {
        coro_ = other.coro_;
        other.coro_ = nullptr;
    }

    ~task() {
        if (coro_) coro_.destroy();
    }

private:
    using handle_t = std::experimental::coroutine_handle<promise_type>;
    task(promise_type *p) : coro_(handle_t::from_promise(*p)) {}
    handle_t coro_;
};

template <typename T>
struct completion {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::experimental::coroutine_handle<> handle) noexcept {
        handle_ = handle;
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

template <typename T>
task<std::vector<T>> taskAll(std::vector<task<T>> list) {
    std::vector<T> result;
    result.reserve(list.size());
    for (auto&& t : list) {
        result.push_back(co_await t);
    }
    co_return result;
}

template <typename T>
completion<T> taskAny_doesnt_work(std::vector<task<T>> list) {
    completion<T> result;
    std::vector<task<T>> tasks;
    tasks.reserve(list.size());
    for (auto& t : list) {
        auto tmp = [&]() mutable -> task<T> {
            try {
                auto res = co_await t;
                if (!result.done()) result.resolve(res);
            } catch (...) {
                if (tasks.size() == list.size()) {
                    result.reject(std::make_exception_ptr("No tasks finished successfully"));
                }
            }
            co_return T{};
        }();
        tmp.start();
        tasks.push_back(std::move(tmp));
    }
    return result;
}
