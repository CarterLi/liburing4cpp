#pragma once

#include <experimental/coroutine>
#include <variant>
#include <optional>

template <typename T = void>
struct promise {
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

template <>
struct promise<void> {
    bool await_ready() const noexcept {
        return false;
    }
    void await_suspend(std::experimental::coroutine_handle<> caller) noexcept {
        handle_ = caller;
    }
    void await_resume() const noexcept {
        if (result_) {
            std::rethrow_exception(result_.value());
        }
    }

    void resolve() {
        handle_.resume();
    }
    void reject(std::exception_ptr eptr) {
        result_.template emplace(eptr);
        handle_.resume();
    }

    bool done() const {
        return handle_.done();
    }

private:
    std::experimental::coroutine_handle<> handle_;
    std::optional<std::exception_ptr> result_;
};
