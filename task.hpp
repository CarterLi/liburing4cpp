#pragma once

// Original source:
// https://github.com/Quuxplusone/coro/blob/master/include/coro/gor_task.h

#include <exception>
#include <experimental/coroutine>
#include <variant>
#include <array>

struct promise_base;
struct task_base;
template <typename T = void>
struct task;

// only for internal usage
template <typename T>
struct task_promise_base {
    task<T> get_return_object();
    auto initial_suspend() { return std::experimental::suspend_never(); }
    auto final_suspend() noexcept {
        struct Awaiter: std::experimental::suspend_always {
            task_promise_base *me_;

            Awaiter(task_promise_base *me): me_(me) {};
            void await_suspend(std::experimental::coroutine_handle<> caller) const noexcept {
                if (me_->then) {
                    me_->then();
                } else if (me_->waiter_) {
                    me_->waiter_.resume();
                }
            }
        };
        return Awaiter(this);
    }
    void unhandled_exception() {
        result_.template emplace<2>(std::current_exception());
    }

    std::function<void ()> then;

protected:
    template <typename TT>
    friend struct task;
    task_promise_base() = default;
    std::experimental::coroutine_handle<> waiter_;
    std::variant<
        std::monostate,
        std::conditional_t<std::is_void_v<T>, std::monostate, T>,
        std::exception_ptr
    > result_;
    std::variant<
        std::monostate,
        task_base *,
        promise_base *
    > callee_;
};

// only for internal usage
template <typename T>
struct task_promise: task_promise_base<T> {
    using task_promise_base<T>::result_;
    using task_promise_base<T>::callee_;

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

// This is NOT a polymorphic class, its destructor is NOT virtual
struct task_base: std::experimental::suspend_always {
    task_base() = default;
    task_base(const task_base&) = delete;
    task_base& operator =(const task_base&) = delete;

    virtual void cancel() = 0;
};

/**
 * An awaitable object that returned by an async function
 * @warning do NOT discard this object when returned by some function, or UB WILL happen
 */
template <typename T>
struct task final: task_base {
    using promise_type = task_promise<T>;
    using handle_t = std::experimental::coroutine_handle<promise_type>;

    template <typename TT>
    void await_suspend(std::experimental::coroutine_handle<task_promise<TT>> caller) noexcept {
        caller.promise().callee_ = this;
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

    /** Get the result hold by this task */
    T get_result() const {
        assert(done());
        return await_resume();
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

    /** Destroy the task object
     * @warning It's allowed to destroy the object before the coroutine is done.
     * @warning The program will try to destroy the internal coroutine handle to avoid memory leaking
     * @warning But IT'S STILL BUGGY
     */
    ~task() {
        if (!coro_) return;
        if (!coro_.done()) {
            coro_.promise().then = [coro_ = coro_] () mutable {
                auto p = coro_.promise();
                // FIXME: Does this do right thing?
                if (p.waiter_) {
                    p.waiter_.destroy();
                }
                coro_.destroy();
            };
        } else {
            coro_.destroy();
        }
    }

    // Only for internal usage
    template <typename Fn>
    void then(Fn&& fn) {
        auto& p = coro_.promise();
        assert(p.then || "Fn `then` has been attached");
        p.then = std::move(fn);
    }

    void cancel() override {
        auto& callee = coro_.promise().callee_;
        assert(callee.index() != 0);
        if (callee.index() == 1) {
            return std::get<1>(callee)->cancel();
        } else {
            return std::get<2>(callee)->cancel();
        }
    }

private:
    friend struct task_promise_base<T>;
    task(promise_type *p): coro_(handle_t::from_promise(*p)) {}
    handle_t coro_;
};

template <typename T>
task<T> task_promise_base<T>::get_return_object() {
    return task<T>(static_cast<task_promise<T> *>(this));
}
