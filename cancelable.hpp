#pragma once

template <typename T, bool nothrow>
struct task;
template <typename T, bool nothrow>
struct promise;

/** Indicate a class is cancelable
 * @warning This is NOT a polymorphic class, its destructor is NOT virtual
 */
struct cancelable {
    virtual void cancel() = 0;

protected:
    inline void on_suspend(cancelable** callee_ref) noexcept {
        *callee_ref = this;
    }

    inline void on_resume() const noexcept {
    }
};

struct cancelable_promise_base {
    template <typename T, bool nothrow>
    friend struct task;
    template <typename T, bool nothrow>
    friend struct promise;

protected:
    cancelable* callee_ = nullptr;
};
