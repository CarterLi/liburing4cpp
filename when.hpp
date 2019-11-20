#pragma once

#include "task.hpp"
#include "promise.hpp"

/** Return a task that will be finished when all given tasks are finished
 * @param tasks tasks to wait
 * @see https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise/all
 * @throw once any task are failed
 */
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
                waiters[i].then([&, ex = std::current_exception()] () {
                    p.reject(ex);
                });
            }
        }(i);
    }
    co_await p;
    co_return result;
}

/** Return a task that will be finished if any given task are finished
 * @param tasks tasks to wait
 * @see https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise/any
 * @throw once all given tasks are rejected
 */
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
                    waiters[i].then([&, ex = std::current_exception()] () {
                        p.reject(ex);
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
                waiters[i].then([&, ex = std::current_exception()] () {
                    p.reject(ex);
                });
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
                        p.reject(ex);
                    });
                }
            }
        }(i);
    }
    co_await p;
}
