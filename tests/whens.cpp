#include "when.hpp"
#include "io_service.hpp"

int main() {
    io_service service;
    auto work = [&service] () -> task<> {
        fmt::print("starting\n");
        co_await service.delay(1);
        fmt::print("ok\n");
        co_await when_any(std::array {
            service.delay(1),
            service.delay(2),
            service.delay(3),
        });
        fmt::print("when_any\n");
        co_await when_all(std::array {
            service.delay(1),
            service.delay(2),
            service.delay(3),
        });
        fmt::print("when_all\n");
    }();

    // Event loop
    while (!work.done()) {
        auto [promise, res] = service.wait_event();

        // Found a finished event, go back to its coroutine.
        promise->resolve(res);
    }

    // exception ( if any ) will be thrown here
    work.get_result();
}
