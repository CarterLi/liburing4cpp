#pragma once
#include <functional>
#include <libwinring.h>

#include "yield.hpp"
#include "io_host.hpp"

// In order to be used inside coroutine, `io_coroutine` must be created with new
// It will be deleted automatically when coroutine exits. See `coro_holder::next()`
class io_coroutine {
public:
    template <typename TEntry>
    explicit io_coroutine(io_host& host, TEntry entry, std::function<void ()> cleanup = std::function<void ()>())
        : host(host)
        , coro([this, entry] (auto&) { entry(*this); })
        , cleanup(std::move(cleanup))
    {
        ++host.running_coroutines;
        coro.next(); // We have to make sure that this->yield() is executed at least once.
    }

    ~io_coroutine() {
        if (cleanup) cleanup();
        --host.running_coroutines;
    }

private:
    struct value_holder final: nextable {
        int value = INT32_MIN;

        void next(int ret) override {
            value = ret;
        }
    };

    struct coro_holder final: nextable {
        coro_holder(io_coroutine* coro): coro(coro) {}

        void next(int ret) override {
            auto* _coro = coro;
            if (!_coro->coro.next(ret)) {
                // coro_holder has been destructed here. `this->coro` must NOT be used
                delete _coro;
            }
        }

private:
        io_coroutine* coro;
    };

    struct prepared_operation {
        prepared_operation(win_ring_sqe* sqe, io_coroutine* coro, uint8_t iflags): sqe(sqe), coro(coro) {
            sqe->Flags |= iflags;
        }

        int await() {
            coro_holder holder(coro);
            win_ring_sqe_set_data(sqe, &holder);
            coro->coro.yield();
            return coro->coro.current().value();
        }

        [[nodiscard]]
        std::unique_ptr<value_holder> promise() {
            auto holder = std::make_unique<value_holder>();
            win_ring_sqe_set_data(sqe, holder.get());
            return holder;
        }

        void detach() {
            // Ignore result
        }

private:
        win_ring_sqe* sqe;
        io_coroutine* coro;
    };

public:
    [[nodiscard]]
    prepared_operation read(HANDLE fd, void *buf, unsigned int nbytes, off_t offset, uint8_t iflags = 0) noexcept {
        auto* sqe = host.get_sqe_safe();
        win_ring_prep_read(sqe, fd, buf, nbytes, offset);
        return prepared_operation(sqe, this, iflags);
    }

    [[nodiscard]]
    prepared_operation write(HANDLE fd, const void *buf, unsigned int nbytes, off_t offset, uint8_t iflags = 0) noexcept {
        auto* sqe = host.get_sqe_safe();
        win_ring_prep_write(sqe, fd, const_cast<void *>(buf), nbytes, offset);
        return prepared_operation(sqe, this, iflags);
    }

    [[nodiscard]]
    prepared_operation nop(uint8_t iflags = 0) noexcept {
        auto* sqe = host.get_sqe_safe();
        win_ring_prep_nop(sqe);
        return prepared_operation(sqe, this, iflags);
    }

    io_host& host;

private:
    FiberSpace::Fiber<int, true> coro;
    std::function<void ()> cleanup;
};
