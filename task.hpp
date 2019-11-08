#pragma once

// Original source:
// https://github.com/Quuxplusone/coro/blob/master/include/coro/gor_task.h

#include <exception>
#include <experimental/coroutine>
#include <variant>

template<class T>
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

    bool await_ready() { return false; }
    void await_suspend(std::experimental::coroutine_handle<void> caller) {
        coro_.promise().waiter_ = caller;
    }
    T await_resume() {
        if (coro_.promise().result_.index() == 2) {
            std::rethrow_exception(std::get<2>(coro_.promise().result_));
        }
        return std::get<1>(coro_.promise().result_);
    }

    T get_result() {
        return await_resume();
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

template <typename T>
task<std::vector<T>> taskAll1(std::vector<task<T>> list) {
    std::vector<T> result;
    result.reserve(list.size());
    for (auto&& t : list) {
        result.push_back(co_await t);
    }
    co_return result;
}

template <typename T>
task<std::vector<T>> taskAll(std::vector<task<T>> list) {
    std::vector<T> result(list.size());
    size_t left = list.size();
    completion<std::vector<T>> promise;
    for (size_t i=0; i<list.size(); ++i) {
        [&, i]() mutable -> task<bool> {
            result[i] = co_await list[i];
            left--;
            if (!left) promise.resolve(std::move(result));
            co_return true;
        }();
    }
    co_await promise;
    co_return result;
}
