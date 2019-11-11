#pragma once

#include "task.hpp"

template <typename T, size_t N>
task<std::array<T, N>> when_all(std::array<task<T>, N> tasks) {
    std::array<T, N> result;
    size_t left = tasks.size();
    std::array<task<>, N> waiters;
    promise<> p;

    for (size_t i=0; i<tasks.size(); ++i) {
        waiters[i] = [&](size_t i) mutable -> task<> {
            try {
                result[i] = co_await tasks[i];
                if (!--left) waiters[i].then([&] () { p.resolve(); });
            } catch (...) {
                waiters[i].then([&] () { p.reject(std::current_exception()); });
            }
        }(i);
    }
    co_await p;
    co_return result;
}

template <typename T, size_t N>
task<T> when_any(std::array<task<T>, N> tasks) {
    T result;
    size_t left = tasks.size();
    std::array<task<>, N> waiters;
    promise<> p;

    for (size_t i=0; i<tasks.size(); ++i) {
        waiters[i] = [&](size_t i) mutable -> task<> {
            try {
                result = co_await tasks[i];
                if (!p.done()) waiters[i].then([&] () { p.resolve(); });
            } catch (...) {
                if (!--left) {
                    waiters[i].then([&] () {
                        p.reject(
                            std::make_exception_ptr(
                                std::runtime_error("All tasks rejected")
                            )
                        );
                    });
                }
            }
        }(i);
    }
    co_await p;
    co_return result;
}

template <size_t N>
task<> when_all(std::array<task<>, N> tasks) {
    size_t left = tasks.size();
    std::array<task<>, N> waiters;
    promise<> p;

    for (size_t i=0; i<tasks.size(); ++i) {
        waiters[i] = [&](size_t i) mutable -> task<> {
            try {
                co_await tasks[i];
                if (!--left) waiters[i].then([&] () { p.resolve(); });
            } catch (...) {
                waiters[i].then([&] () { p.reject(std::current_exception()); });
            }
        }(i);
    }
    co_await p;
}

template <size_t N>
task<> when_any(std::array<task<>, N> tasks) {
    size_t left = tasks.size();
    std::array<task<>, N> waiters;
    promise<> p;

    for (size_t i=0; i<tasks.size(); ++i) {
        waiters[i] = [&](size_t i) mutable -> task<> {
            try {
                co_await tasks[i];
                if (!p.done()) waiters[i].then([&] () { p.resolve(); });
            } catch (...) {
                if (!--left) {
                    waiters[i].then([&, ex = std::current_exception()] () {
                        try {
                            try {
                                std::rethrow_exception(ex);
                            } catch (...) {
                                std::throw_with_nested(std::runtime_error("All tasks rejected"));
                            }
                        } catch (...) {
                            p.reject(std::current_exception());
                        }
                    });
                }
            }
        }(i);
    }
    co_await p;
}
