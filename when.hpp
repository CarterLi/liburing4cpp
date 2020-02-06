#pragma once

#include <optional>
#include "task.hpp"

/** Return a task that will be finished when all given tasks are finished
 * @param tasks tasks to wait
 * @see https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise/all
 * @throw once any task are failed
 */
template <typename T, bool nothrow, size_t N>
task<std::array<T, N>> when_all(std::array<task<T, nothrow>, N> tasks) noexcept(nothrow) {
    static_assert(N > 0);

    std::array<T, N> result;
    std::array<task<void, true>, N> waiters;
    std::exception_ptr ex;

    for (size_t i=0; i<tasks.size(); ++i) {
        waiters[i] = [&](size_t i) mutable -> task<void, true> {
            if constexpr (!nothrow) {
                try {
                    result[i] = co_await tasks[i];
                } catch (...) {
                    if (ex) co_return;
                    ex = std::current_exception();
                    for (auto& task : tasks) {
                        if (!task.done()) task.cancel();
                    }
                }
            } else {
                result[i] = co_await tasks[i];
            }
        }(i);
    }
    for (auto& waiter : waiters) {
        co_await waiter;
    }
    if constexpr (!nothrow) {
        if (ex) std::rethrow_exception(ex);
    }
    co_return result;
}

/** Return a task that will be finished if any given task are finished
 * @param tasks tasks to wait
 * @see https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise/any
 * @throw once all given tasks are rejected
 */
template <typename T, bool nothrow, size_t N>
task<T> when_any(std::array<task<T, nothrow>, N> tasks) noexcept(nothrow) {
    static_assert(N > 0);

    std::optional<T> result;
    std::array<task<void, true>, N> waiters;

    for (size_t i=0; i<tasks.size(); ++i) {
        waiters[i] = [&](size_t i) mutable -> task<void, true> {
            try {
                auto ret = co_await tasks[i];
                if (result) co_return;
                result = std::move(ret);
                for (auto& task : tasks) {
                    if (!task.done()) task.cancel();
                }
            } catch (...) {
                if constexpr (nothrow) {
                    __builtin_unreachable();
                }
            }
        }(i);
    }
    for (auto& waiter : waiters) {
        co_await waiter;
    }
    if constexpr (!nothrow) {
        if (!result) throw std::runtime_error("All tasks failed");
    }
    co_return std::move(result).value();
}

/** Return a task that will be finished when all given tasks are finished
 * @param tasks tasks to wait
 * @see https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Promise/all
 * @throw once any task are failed
 */
template <bool nothrow, size_t N>
task<> when_all(std::array<task<void, nothrow>, N> tasks) {
    static_assert(N > 0);

    std::array<task<void, true>, N> waiters;
    std::exception_ptr ex;

    for (size_t i=0; i<tasks.size(); ++i) {
        waiters[i] = [&](size_t i) mutable -> task<void, true> {
            if constexpr (!nothrow) {
                try {
                    co_await tasks[i];
                } catch (...) {
                    if (ex) co_return;
                    ex = std::current_exception();
                    for (auto& task : tasks) {
                        if (!task.done()) task.cancel();
                    }
                }
            } else {
                co_await tasks[i];
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
template <bool nothrow, size_t N>
task<> when_any(std::array<task<void, nothrow>, N> tasks) {
    static_assert(N > 0);

    bool ok = false;
    std::array<task<void, true>, N> waiters;

    for (size_t i=0; i<tasks.size(); ++i) {
        waiters[i] = [&](size_t i) mutable -> task<void, true> {
            try {
                co_await tasks[i];
                if (ok) co_return;
                ok = true;
                for (auto& task : tasks) {
                    if (!task.done()) task.cancel();
                }
            } catch (...) {
                if constexpr (nothrow) {
                    __builtin_unreachable();
                }
            }
        }(i);
    }
    for (auto& waiter : waiters) {
        co_await waiter;
    }
    if constexpr (!nothrow) {
        if (!ok) throw std::runtime_error("All tasks failed");
    }
}
