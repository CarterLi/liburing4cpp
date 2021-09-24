#include <chrono>
#include <fmt/chrono.h> // https://github.com/fmtlib/fmt

#include "io_service.hpp"

int main() {
    io_service service;

    service.run([] (io_service& service) -> task<> {
        auto delayAndPrint = [&] (int second, uint8_t iflags = 0) -> task<> {
            auto ts = dur2ts(std::chrono::seconds(second));
            co_await service.timeout(&ts, iflags) | panic_on_err("timeout", false);
            fmt::print("{:%T}: delayed {}s\n", std::chrono::system_clock::now().time_since_epoch(), second);
        };

        fmt::print("in sequence start\n");
        co_await delayAndPrint(1);
        co_await delayAndPrint(2);
        co_await delayAndPrint(3);
        fmt::print("in sequence end, should wait 6s\n\n");

        fmt::print("io link start\n");
        delayAndPrint(1, IOSQE_IO_HARDLINK);
        delayAndPrint(2, IOSQE_IO_HARDLINK);
        co_await delayAndPrint(3);
        fmt::print("io link end, should wait 6s\n");
    }(service));
}
