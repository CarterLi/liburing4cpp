#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <chrono>
#include <cerrno>

#include "global.hpp"
#include "async_coro.hpp"

task<bool> start() {
    // TODO: less verbose code
    std::vector<task<int>> vec;
    vec.emplace_back(async_delay(1));
    vec.emplace_back(async_delay(2));
    vec.emplace_back(async_delay(3));
    co_await taskAll<int>(std::move(vec));
    co_return true;
}

int main(int argc, char* argv[]) {
#if !USE_LIBAIO
    if (io_uring_queue_init(32, &ring, 0)) panic("queue_init");
    on_scope_exit closerg([&]() { io_uring_queue_exit(&ring); });
#else
    if (io_queue_init(32, &context)) panic("queue_init");
    on_scope_exit closerg([&]() { io_queue_release(context); });
#endif

    auto work = start();

    // Event loop
    while (!work.done()) {
        auto [promise, res] = wait_for_event();

        // Found a finished event, go back to its coroutine.
        try {
            promise->resolve(res);
        } catch (std::runtime_error& e) {
            fmt::print("{}\n", e.what());
        }
    }
}
