#pragma once
#include <functional>
#include <string_view>
#include <stdexcept>
#include <system_error>
#include <cstdio>
#include <cerrno>
#include <cassert>
#include <liburing.h>

#include "utils.hpp"

struct nextable {
    virtual void next(int ret) = 0;
};

class io_host {
public:
    io_host(int entries, int flags = 0) {
        if (io_uring_queue_init(entries, &ring, flags) < 0) panic("queue_init");

        auto* probe = io_uring_get_probe_ring(&ring);
        on_scope_exit free_probe([=]() { io_uring_free_probe(probe); });

#ifndef NDEBUG
#   define TEST_IORING_OP(opcode) do {\
        for (int i = 0; i < probe->ops_len; ++i) {\
            if (probe->ops[i].op == opcode && probe->ops[i].flags & IO_URING_OP_SUPPORTED) {\
                    probe_ops[i] = true;\
                    puts("\t" #opcode);\
                    break;\
                }\
            }\
        } while (0)
        puts("Supported io_uring opcodes by current kernel:");
        TEST_IORING_OP(IORING_OP_NOP);
        TEST_IORING_OP(IORING_OP_READV);
        TEST_IORING_OP(IORING_OP_WRITEV);
        TEST_IORING_OP(IORING_OP_FSYNC);
        TEST_IORING_OP(IORING_OP_READ_FIXED);
        TEST_IORING_OP(IORING_OP_WRITE_FIXED);
        TEST_IORING_OP(IORING_OP_POLL_ADD);
        TEST_IORING_OP(IORING_OP_POLL_REMOVE);
        TEST_IORING_OP(IORING_OP_SYNC_FILE_RANGE);
        TEST_IORING_OP(IORING_OP_SENDMSG);
        TEST_IORING_OP(IORING_OP_RECVMSG);
        TEST_IORING_OP(IORING_OP_TIMEOUT);
        TEST_IORING_OP(IORING_OP_TIMEOUT_REMOVE);
        TEST_IORING_OP(IORING_OP_ACCEPT);
        TEST_IORING_OP(IORING_OP_ASYNC_CANCEL);
        TEST_IORING_OP(IORING_OP_LINK_TIMEOUT);
        TEST_IORING_OP(IORING_OP_CONNECT);
        TEST_IORING_OP(IORING_OP_FALLOCATE);
        TEST_IORING_OP(IORING_OP_OPENAT);
        TEST_IORING_OP(IORING_OP_CLOSE);
        TEST_IORING_OP(IORING_OP_FILES_UPDATE);
        TEST_IORING_OP(IORING_OP_STATX);
        TEST_IORING_OP(IORING_OP_READ);
        TEST_IORING_OP(IORING_OP_WRITE);
        TEST_IORING_OP(IORING_OP_FADVISE);
        TEST_IORING_OP(IORING_OP_MADVISE);
        TEST_IORING_OP(IORING_OP_SEND);
        TEST_IORING_OP(IORING_OP_RECV);
        TEST_IORING_OP(IORING_OP_OPENAT2);
        TEST_IORING_OP(IORING_OP_EPOLL_CTL);
        TEST_IORING_OP(IORING_OP_SPLICE);
        TEST_IORING_OP(IORING_OP_PROVIDE_BUFFERS);
        TEST_IORING_OP(IORING_OP_REMOVE_BUFFERS);
        TEST_IORING_OP(IORING_OP_TEE);
        TEST_IORING_OP(IORING_OP_SHUTDOWN);
        TEST_IORING_OP(IORING_OP_RENAMEAT);
        TEST_IORING_OP(IORING_OP_UNLINKAT);
        TEST_IORING_OP(IORING_OP_MKDIRAT);
        TEST_IORING_OP(IORING_OP_SYMLINKAT);
        TEST_IORING_OP(IORING_OP_LINKAT);
#   undef TEST_IORING_OP

#   define TEST_IORING_FEATURE(feature) if (ring.features & feature) puts("\t" #feature)
        puts("Supported io_uring features by current kernel:");
        TEST_IORING_FEATURE(IORING_FEAT_SINGLE_MMAP);
        TEST_IORING_FEATURE(IORING_FEAT_NODROP);
        TEST_IORING_FEATURE(IORING_FEAT_SUBMIT_STABLE);
        TEST_IORING_FEATURE(IORING_FEAT_RW_CUR_POS);
        TEST_IORING_FEATURE(IORING_FEAT_CUR_PERSONALITY);
        TEST_IORING_FEATURE(IORING_FEAT_FAST_POLL);
        TEST_IORING_FEATURE(IORING_FEAT_POLL_32BITS);
        TEST_IORING_FEATURE(IORING_FEAT_SQPOLL_NONFIXED);
        TEST_IORING_FEATURE(IORING_FEAT_EXT_ARG);
        TEST_IORING_FEATURE(IORING_FEAT_NATIVE_WORKERS);
        TEST_IORING_FEATURE(IORING_FEAT_RSRC_TAGS);
#   undef TEST_IORING_FEATURE
#endif
    }

    ~io_host() {
        io_uring_queue_exit(&ring);
    }

    [[nodiscard]]
    io_uring_sqe* io_uring_get_sqe_safe() noexcept {
        auto* sqe = io_uring_get_sqe(&ring);
        if (__builtin_expect(!!sqe, true)) {
            return sqe;
        } else {
#ifndef NDEBUG
            printf(__FILE__ ": SQ is full, flushing %u cqe(s)\n", cqe_count);
#endif
            io_uring_cq_advance(&ring, cqe_count);
            cqe_count = 0;
            io_uring_submit(&ring);
            sqe = io_uring_get_sqe(&ring);
            assert(sqe && "sqe should not be NULL");
            return sqe;
        }
    }

    void run() {
        while (running_coroutines > 0) {
            io_uring_submit_and_wait(&ring, 1);

            io_uring_cqe *cqe;
            unsigned head;

            io_uring_for_each_cqe(&ring, head, cqe) {
                ++cqe_count;
                auto p = static_cast<nextable *>(io_uring_cqe_get_data(cqe));
                if (p) p->next(cqe->res);
            }

#ifndef NDEBUG
            printf(__FILE__ ": Found %u cqe(s), looping...\n", cqe_count);
#endif
            io_uring_cq_advance(&ring, cqe_count);
            cqe_count = 0;
        }
    }

    io_uring ring;
    int cqe_count = 0;
    bool probe_ops[IORING_OP_LAST] = {};
    int running_coroutines = 0;
};
