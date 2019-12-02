#pragma once

#include "task.hpp"

/** Return a task that will be finished when all given tasks are finished
 * @param tasks tasks to wait
 * @see https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise/all
 * @throw once any task are failed
 */
template <typename T, size_t N>
task<std::array<T, N>> when_all(std::array<task<T>, N> tasks) {
    std::array<T, N> result;
    std::array<task<>, N> waiters;
    std::exception_ptr ex;

    for (size_t i=0; i<tasks.size(); ++i) {
        waiters[i] = [&](size_t i) mutable -> task<> {
            try {
                result[i] = co_await tasks[i];
            } catch (...) {
                if (ex) co_return;
                ex = std::current_exception();
                for (auto& task : tasks) {
                    if (!task.done()) task.cancel();
                }
            }
        }(i);
    }
    for (auto& waiter : waiters) {
        co_await waiter;
    }
    if (ex) std::rethrow_exception(ex);
    co_return result;
}

/** Return a task that will be finished if any given task are finished
 * @param tasks tasks to wait
 * @see https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise/any
 * @throw once all given tasks are rejected
 */
template <typename T, size_t N>
task<T> when_any(std::array<task<T>, N> tasks) {
    std::optional<T> result;
    std::array<task<>, N> waiters;

    for (size_t i=0; i<tasks.size(); ++i) {
        waiters[i] = [&](size_t i) mutable -> task<> {
            try {
                auto ret = co_await tasks[i];
                if (result) co_return;
                result = std::move(ret);
                for (auto& task : tasks) {
                    if (!task.done()) task.cancel();
                }
            } catch (...) {
            }
        }(i);
    }
    for (auto& waiter : waiters) {
        co_await waiter;
    }
    if (!result) throw std::runtime_error("All tasks failed");
    co_return std::move(result).value();
}

/** Return a task that will be finished when all given tasks are finished
 * @param tasks tasks to wait
 * @see https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise/all
 * @throw once any task are failed
 */
template <size_t N>
task<> when_all(std::array<task<>, N> tasks) {
    std::array<task<>, N> waiters;
    std::exception_ptr ex;

    for (size_t i=0; i<tasks.size(); ++i) {
        waiters[i] = [&](size_t i) mutable -> task<> {
            try {
                co_await tasks[i];
            } catch (...) {
                if (ex) co_return;
                ex = std::current_exception();
                for (auto& task : tasks) {
                    if (!task.done()) task.cancel();
                }
            }
        }(i);
    }
    for (auto& waiter : waiters) {
        co_await waiter;
    }
    if (ex) std::rethrow_exception(ex);
}

/** Return a task that will be finished if any given task are finished
 * @param tasks tasks to wait
 * @see https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise/any
 * @throw once all given tasks are rejected
 */
template <size_t N>
task<> when_any(std::array<task<>, N> tasks) {
    bool ok = false;
    std::array<task<>, N> waiters;

    for (size_t i=0; i<tasks.size(); ++i) {
        waiters[i] = [&](size_t i) mutable -> task<> {
            try {
                co_await tasks[i];
                if (ok) co_return;
                ok = true;
                for (auto& task : tasks) {
                    if (!task.done()) task.cancel();
                }
            } catch (...) {
            }
        }(i);
    }
    for (auto& waiter : waiters) {
        co_await waiter;
    }
    if (!ok) throw std::runtime_error("All tasks failed");
}
