#pragma once

template <typename T>
struct task;
template <typename T>
struct promise;

/** Indicate a class is cancelable
 * @warning This is NOT a polymorphic class, its destructor is NOT virtual
 */
struct cancelable {
    virtual void cancel() = 0;

protected:
    inline void on_suspended(cancelable** callee_ref) noexcept {
        *callee_ref = this;
#ifndef NDEBUG
        callee_ref_ = callee_ref;
#endif
    }

    inline void on_resume() const noexcept {
#ifndef NDEBUG
        *callee_ref_ = nullptr;
#endif
    }

#ifndef NDEBUG
    cancelable** callee_ref_; // Weak ref
#endif
};

struct cancelable_promise_base {
    template <typename T>
    friend struct task;
    template <typename T>
    friend struct promise;

protected:
    cancelable* callee_ = nullptr;
};
