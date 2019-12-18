#include <chrono>
#include <fmt/chrono.h> // https://github.com/fmtlib/fmt

#include "when.hpp"
#include "io_service.hpp"

using namespace std::chrono_literals;

int main() {
    io_service service;

    auto work = [] (io_service& service) -> task<> {
        auto delayAndPrint = [&] (int second, uint8_t iflags = 0) -> task<> {
            co_await service.delay({ second, 0 }, iflags) | panic_on_err("delay", false);
            fmt::print("{:%T}: delayed {}s\n", std::chrono::system_clock::now().time_since_epoch(), second);
        };

        fmt::print("in sequence start\n");
        co_await delayAndPrint(1);
        co_await delayAndPrint(2);
        co_await delayAndPrint(3);
        fmt::print("in sequence end, should wait 6s\n\n");
        fmt::print("when any start\n");
        co_await when_any(std::array {
            delayAndPrint(1),
            delayAndPrint(2),
            delayAndPrint(3),
        });
        fmt::print("when any end, should wait 1s\n\n");
        fmt::print("when all start\n");
        co_await when_all(std::array {
            delayAndPrint(1),
            delayAndPrint(2),
            delayAndPrint(3),
        });
        fmt::print("when all end, should wait 3s\n\n");
        fmt::print("cancel start\n");
        auto t = delayAndPrint(1);
        t.cancel();
        try {
            co_await t;
        } catch (std::exception& ex) {
            fmt::print("exception cached: {}; it's expected\n", ex.what());
        }
        fmt::print("cancel end, should not wait\n");
        fmt::print("io link start\n");
        delayAndPrint(1, IOSQE_IO_LINK | IOSQE_IO_HARDLINK);
        delayAndPrint(2, IOSQE_IO_LINK | IOSQE_IO_HARDLINK);
        co_await delayAndPrint(3);
        fmt::print("io link end, should wait 6s\n");
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
