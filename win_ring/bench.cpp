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
            auto* sqe = win_ring_get_sqe(ring);
            win_ring_prep_nop(sqe);
            win_ring_submit_and_wait(ring, 1);

            win_ring_cqe *cqe = win_ring_peek_cqe(ring);
            (void) cqe->ResultCode;
            win_ring_cqe_seen(ring, cqe);
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
            _mm_pause();
        }
    }
}
