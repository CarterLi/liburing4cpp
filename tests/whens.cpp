#include "when.hpp"
#include "global.hpp"
#include "async_coro.hpp"

int main() {
    if (io_uring_queue_init(32, &ring, 0)) panic("queue_init");
    on_scope_exit closerg([&]() { io_uring_queue_exit(&ring); });

    auto work = [] () -> task<> {
        co_await when_any(std::array {
            async_delay(1),
            async_delay(2),
            async_delay(3),
        });
        fmt::print("when_any\n");
        co_await when_all(std::array {
            async_delay(1),
            async_delay(2),
            async_delay(3),
        });
        fmt::print("when_all\n");
    }();

    // Event loop
    while (!work.done()) {
        auto [promise, res] = wait_for_event();

        // Found a finished event, go back to its coroutine.
        promise->resolve(res);
    }
}
