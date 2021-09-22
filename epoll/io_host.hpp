#pragma once
#include <unistd.h>
#if USE_LIBAIO
#   include <libaio.h>
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
#else
        if ((epfd = epoll_create1(0)) < 0) panic("epoll_create1");
#endif
    }

    ~io_host() {
#if USE_LIBAIO
        io_queue_release(ioctx);
#else
        close(epfd);
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
        if (size < 0) panic("io_getevents");
        for (int i = 0; i < size; ++i) {
            auto& event = events[i];
            auto* coro = (nextable*)event.data;
            coro->next(event.res);
        }
#   ifndef NDEBUG
        syscall_count += 2;
#   endif
    }
#else
    std::vector<epoll_event> events(entries);
    while (running_coroutines > 0) {
        int size = epoll_wait(epfd, events.data(), events.size(), -1);
#       ifndef NDEBUG
        ++syscall_count;
#       endif
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
#else
        epoll_event ev = {
            .events = poll_mask | EPOLLONESHOT | EPOLLET,
            .data = { .ptr = data },
        };

        if (epoll_ctl(epfd, polling_fds.emplace(fd).second ? EPOLL_CTL_ADD : EPOLL_CTL_MOD, fd, &ev) < 0) panic("epoll_ctl");
#   ifndef NDEBUG
        ++syscall_count;
#   endif
#endif
    }

    void remove_poll([[maybe_unused]] int fd) {
#if !USE_LIBAIO
        polling_fds.erase(fd);
        if (epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr) < 0) panic("epoll_ctl");
#   ifndef NDEBUG
        ++syscall_count;
#   endif
#endif
    }

#if USE_LIBAIO
    io_context_t ioctx;
    std::vector<iocb> pending_reqs;
#else
    int epfd;
    std::set<int> polling_fds;
#endif
    int running_coroutines = 0;
    int entries;

#ifndef NDEBUG
    unsigned syscall_count = 0;
#endif
};
