#include <fmt/format.h> // https://github.com/fmtlib/fmt

#include "when.hpp"
#include "io_service.hpp"

using namespace std::chrono_literals;

int main() {
    io_service service;

    auto work = [] (io_service& service) -> task<> {
        auto delayAndPrint = [&] (int second) -> task<> {
            co_await service.delay({ second, 0 });
            fmt::print("delayed: {}s\n", second);
        };

        fmt::print("starting\n");
        fmt::print("in sequence\n");
        co_await delayAndPrint(1);
        co_await delayAndPrint(2);
        co_await delayAndPrint(3);
        fmt::print("in sequence end\n\n");
        fmt::print("when any\n");
        co_await when_any(std::array {
            delayAndPrint(1),
            delayAndPrint(2),
            delayAndPrint(3),
        });
        fmt::print("when any end\n\n");
        fmt::print("when all\n");
        co_await when_all(std::array {
            delayAndPrint(1),
            delayAndPrint(2),
            delayAndPrint(3),
        });
        fmt::print("when all end\n\n");
        fmt::print("cancel\n");
        auto t = delayAndPrint(1);
        t.cancel();
        try {
            co_await t;
        } catch (...) {
            fmt::print("cancel end\n");
        }
    }(service);

    // Event loop
    while (!work.done()) {
        auto [promise, res] = service.wait_event();

        // Found a finished event, go back to its coroutine.
        promise->resolve(res);
    }

    // exception ( if any ) will be thrown here
    work.get_result();
}
