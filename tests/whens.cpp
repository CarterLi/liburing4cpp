#include <fmt/format.h> // https://github.com/fmtlib/fmt

#include "when.hpp"
#include "io_service.hpp"

using namespace std::chrono_literals;

int main() {
    io_service service;
    auto work = [] (io_service& service) -> task<> {
        fmt::print("starting\n");
        co_await service.delay(1s);
        co_await service.delay(2s);
        co_await service.delay(3s);
        fmt::print("in sequence\n");
        co_await when_any(std::array {
            service.delay(1s),
            service.delay(2s),
            service.delay(3s),
        });
        fmt::print("when any\n");
        co_await when_all(std::array {
            service.delay(1s),
            service.delay(2s),
            service.delay(3s),
        });
        fmt::print("when all\n");
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
