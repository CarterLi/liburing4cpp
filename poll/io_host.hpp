#pragma once
#include <unistd.h>
#if USE_LIBAIO
#   include <libaio.h>
#elif USE_LIBURING
#   include <liburing.h>
#else
#   include <sys/epoll.h>
#endif
#include <vector>
#include <set>
#include "utils.hpp"
#include "yield.hpp"

struct nextable {
    virtual void next(int ret) = 0;
};

class io_host {
public:
    io_host([[maybe_unused]] int entries): entries(entries) {
#if USE_LIBAIO
        if (int ret = io_queue_init(entries, &ioctx); ret < 0) panic("io_queue_init", -ret);
#elif USE_LIBURING
        if (io_uring_queue_init(entries, &ring, 0) < 0) panic("io_uring_queue_init");
#else
        if ((epfd = epoll_create1(0)) < 0) panic("epoll_create1");
#endif
#ifdef SYSCALL_COUNT
        ++syscall_count;
#endif
    }

    ~io_host() {
#if USE_LIBAIO
        io_queue_release(ioctx);
#elif USE_LIBURING
        io_uring_queue_exit(&ring);
#else
        close(epfd);
#endif
#ifdef SYSCALL_COUNT
        ++syscall_count;
#endif
    }

    void run() {
#if USE_LIBAIO
        std::vector<iocb *> iocbps;
        std::vector<io_event> events(entries);
        while (running_coroutines > 0) {
            std::transform(pending_reqs.begin(), pending_reqs.end(), std::back_inserter(iocbps), [](iocb& iocb) { return &iocb; });
            io_submit(ioctx, iocbps.size(), iocbps.data());
            iocbps.resize(0);
            pending_reqs.resize(0);

            int size = io_getevents(ioctx, 1, events.size(), events.data(), nullptr);
#   ifdef SYSCALL_COUNT
            syscall_count += 2;
#   endif
            if (size < 0) panic("io_getevents");
            for (int i = 0; i < size; ++i) {
                auto& event = events[i];
                auto* coro = (nextable*)event.data;
                coro->next(event.res);
            }
        }
#elif USE_LIBURING
        while (running_coroutines > 0) {
            if (io_uring_submit_and_wait(&ring, 1) < 0) panic("io_uring_submit_and_wait");
#   ifdef SYSCALL_COUNT
            ++syscall_count;
#   endif

            io_uring_cqe *cqe;
            unsigned head;
            io_uring_for_each_cqe(&ring, head, cqe) {
                ++cqe_count;
                auto p = static_cast<nextable *>(io_uring_cqe_get_data(cqe));
                if (p) p->next(cqe->res);
            }

            io_uring_cq_advance(&ring, cqe_count);
            cqe_count = 0;
        }
#else
        std::vector<epoll_event> events(entries);
        while (running_coroutines > 0) {
            int size = epoll_wait(epfd, events.data(), events.size(), -1);
#   ifdef SYSCALL_COUNT
            ++syscall_count;
#   endif
            if (size < 0) panic("epoll_wait");
            for (int i = 0; i < size; ++i) {
                auto& event = events[i];
                auto* coro = (nextable*)event.data.ptr;
                coro->next(event.events);
            }
        }
#endif
    }

    void prep_poll(int fd, uint32_t poll_mask, nextable* data) {
#if USE_LIBAIO
        iocb iocb;
        io_prep_poll(&iocb, fd, poll_mask);
        iocb.data = data;
        pending_reqs.push_back(std::move(iocb));
#elif USE_LIBURING
        auto* sqe = io_uring_get_sqe(&ring);
        assert(sqe && "sqe should not be NULL");
        io_uring_prep_poll_add(sqe, fd, poll_mask);
        io_uring_sqe_set_data(sqe, data);
#else
        epoll_event ev = {
            .events = poll_mask | EPOLLONESHOT | EPOLLET,
            .data = { .ptr = data },
        };

        if (epoll_ctl(epfd, polling_fds.emplace(fd).second ? EPOLL_CTL_ADD : EPOLL_CTL_MOD, fd, &ev) < 0) panic("epoll_ctl");
#   ifdef SYSCALL_COUNT
        ++syscall_count;
#   endif
#endif
    }

    void remove_poll([[maybe_unused]] int fd) {
#if !USE_LIBAIO && !USE_LIBURING
        polling_fds.erase(fd);
        if (epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr) < 0) panic("epoll_ctl");
#   ifdef SYSCALL_COUNT
        ++syscall_count;
#   endif
#endif
    }

#if USE_LIBAIO
    io_context_t ioctx;
    std::vector<iocb> pending_reqs;
#elif USE_LIBURING
    io_uring ring;
    int cqe_count = 0;
#else
    int epfd;
    std::set<int> polling_fds;
#endif
    int running_coroutines = 0;
    int entries;

#ifdef SYSCALL_COUNT
    unsigned syscall_count = 0;
#endif
};
