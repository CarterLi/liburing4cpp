#pragma once
#include <unistd.h>
#include <vector>
#include <set>

#include "utils.hpp"
#include "yield.hpp"
#include "io_host.hpp"

// In order to be used inside coroutine, `io_coroutine` must be created with new
// It will be deleted automatically when coroutine exits. See io_coroutine::next
class io_coroutine: nextable {
public:
    template <typename TEntry>
    explicit io_coroutine(io_host& host, TEntry entry, std::function<void ()> cleanup = std::function<void ()>())
        : host(host)
        , cleanup(std::move(cleanup))
        , coro([this, entry] (auto&) { entry(*this); })
    {
        ++host.running_coroutines;
        coro.next(); // We have to make sure that this->yield() is executed at least once.
    }

    ~io_coroutine() {
        if (cleanup) cleanup();
        --host.running_coroutines;
    }

    uint32_t await_poll(int fd, uint32_t poll_mask) {
        host.prep_poll(fd, poll_mask, this);
        coro.yield();
        return coro.current().value();
    }

    void remove_poll(int fd) {
        host.remove_poll(fd);
    }

    void next(int ret) override {
        if (!coro.next(ret)) delete this;
    }

    io_host& host;

private:
    FiberSpace::Fiber<uint32_t, true> coro;
    std::function<void ()> cleanup;
};
