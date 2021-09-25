#include <chrono>
#include <thread>
#include <fmt/format.h> // https://github.com/fmtlib/fmt

#include <liburing/io_service.hpp>

struct stopwatch {
    stopwatch(std::string_view str_): str(str_) {}
    ~stopwatch() {
        fmt::print("{:<20}{:>12}\n", str, (clock::now() - start).count());
    }

    using clock = std::chrono::high_resolution_clock;
    std::string_view str;
    clock::time_point start = clock::now();
};

int main() {
    using uio::io_service;
    using uio::task;

    io_service service;
    const auto iteration = 10000000;

    service.run([] (io_service& service) -> task<> {
        {
            stopwatch sw("service.yield:");
            for (int i = 0; i < iteration; ++i) {
                co_await service.yield();
            }
        }
        {
            stopwatch sw("plain IORING_OP_NOP:");
            for (int i = 0; i < iteration; ++i) {
                auto* ring = &service.get_handle();
                auto* sqe = io_uring_get_sqe(ring);
                io_uring_prep_nop(sqe);
                io_uring_submit_and_wait(ring, 1);

                io_uring_cqe *cqe;
                io_uring_peek_cqe(ring, &cqe);
                (void) cqe->res;
                io_uring_cqe_seen(ring, cqe);
            }
        }
        {
            stopwatch sw("this_thread::yield:");
            for (int i = 0; i < iteration; ++i) {
                std::this_thread::yield();
            }
        }
        {
            stopwatch sw("pause:");
            for (int i = 0; i < iteration; ++i) {
                __builtin_ia32_pause();
            }
        }
    }(service));
}
