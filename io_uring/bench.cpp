#include <chrono>
#include <string_view>
#include <thread>
#include <fmt/format.h>

#include "io_coroutine.hpp"

struct stopwatch {
    stopwatch(std::string_view str_): str(str_) {}
    ~stopwatch() {
        fmt::print("{:<20}{:>12}\n", str, (clock::now() - start).count());
    }

    using clock = std::chrono::high_resolution_clock;
    std::string_view str;
    clock::time_point start = clock::now();
};

int main(int argc, char *argv[]) noexcept {
    io_host host(24);
    const auto iteration = 10000000;

    new io_coroutine(host, [](io_coroutine& coro) {
        {
            stopwatch sw("coro.nop().await()");
            for (int i = 0; i < iteration; ++i) {
                coro.nop().await();
            }
        }
    });
    host.run();

    {
        stopwatch sw("plain IORING_OP_NOP:");
        for (int i = 0; i < iteration; ++i) {
            auto* ring = &host.ring;
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
}
