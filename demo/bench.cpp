#include <chrono>
#include <thread>
#include <fmt/format.h> // https://github.com/fmtlib/fmt

#include "io_service.hpp"

int main() {
    io_service service;
    const auto iteration = 10000000;

    auto work = [] (io_service& service) -> task<> {
        {
            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < iteration; ++i) {
                co_await service.yield();
            }
            fmt::print("{:<20}{:>12}\n", "service.yield:", (std::chrono::high_resolution_clock::now() - start).count());
        }
        {
            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < iteration; ++i) {
                auto* ring = &service.get_handle();
                auto* sqe = io_uring_get_sqe_safe(ring);
                io_uring_prep_nop(sqe);
                io_uring_submit(ring);

                io_uring_cqe *cqe;
                io_uring_wait_cqe(ring, &cqe);
                io_uring_cqe_seen(ring, cqe);
            }
            fmt::print("{:<20}{:>12}\n", "plain IORING_OP_NOP:", (std::chrono::high_resolution_clock::now() - start).count());
        }
        {
            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < iteration; ++i) {
                std::this_thread::yield();
            }
            fmt::print("{:<20}{:>12}\n", "this_thread::yield:", (std::chrono::high_resolution_clock::now() - start).count());
        }
        {
            auto start = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < iteration; ++i) {
                __builtin_ia32_pause();
            }
            fmt::print("{:<20}{:>12}\n", "pause:", (std::chrono::high_resolution_clock::now() - start).count());
        }
    }(service);

    while (!work.done()) {
        auto [promise, res] = service.wait_event();

        promise->resolve(res);
    }

    work.get_result();
}
