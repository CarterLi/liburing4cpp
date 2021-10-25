#pragma once
#include <functional>
#include <string_view>
#include <stdexcept>
#include <system_error>
#include <cstdio>
#include <cerrno>
#include <cassert>
#include <libwinring.h>

#include "utils.hpp"

struct nextable {
    virtual void next(int ret) = 0;
};

class io_host {
public:
    io_host(int entries = 0) {
        if (win_ring_queue_init(entries, &ring)) panic("win_ring_queue_init");
#ifdef SYSCALL_COUNT
        ++syscall_count;
#endif

    }

    ~io_host() {
        win_ring_queue_exit(&ring);
#ifdef SYSCALL_COUNT
        ++syscall_count;
#endif
    }

    [[nodiscard]]
    win_ring_sqe* get_sqe_safe() noexcept {
        auto* sqe = win_ring_get_sqe(&ring);
        if (sqe) { [[likely]]
            return sqe;
        } else {
#ifndef NDEBUG
            printf(__FILE__ ": SQ is full, flushing %u cqe(s)\n", cqe_count);
#endif
            win_ring_cq_advance(&ring, cqe_count);
            cqe_count = 0;
            if (win_ring_submit(&ring) < 0) panic("win_ring_submit");
#ifdef SYSCALL_COUNT
            ++syscall_count;
#endif
            sqe = win_ring_get_sqe(&ring);
            if (!sqe) [[unlikely]] panic("io_uring_get_sqe", ENOMEM /* Use Win32 error code */);
            return sqe;
        }
    }

    void run() {
        while (running_coroutines > 0) {
            if (!win_ring_cq_ready(&ring)) {
                if (win_ring_submit_and_wait(&ring, 1) < 0) panic("win_ring_submit_and_wait");
#ifdef SYSCALL_COUNT
                ++syscall_count;
#endif
            }


            win_ring_cqe *cqe;
            unsigned head;
            win_ring_for_each_cqe(&ring, head, cqe) {
                ++cqe_count;
                auto p = static_cast<nextable *>(win_ring_cqe_get_data(cqe));
                // https://devblogs.microsoft.com/oldnewthing/20061103-07/?p=29133
                if (p) p->next(!SUCCEEDED(cqe->ResultCode) ? cqe->Information : -(int)(DWORD)cqe->ResultCode);
            }

#ifndef NDEBUG
            printf(__FILE__ ": Found %u cqe(s), looping...\n", cqe_count);
#endif
            win_ring_cq_advance(&ring, cqe_count);
            cqe_count = 0;
        }
    }

    win_ring ring;
    int cqe_count = 0;
    int running_coroutines = 0;
#ifdef SYSCALL_COUNT
    unsigned syscall_count = 0;
#endif
};
