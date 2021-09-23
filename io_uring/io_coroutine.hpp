#pragma once
#include <functional>
#include <liburing.h>   // http://git.kernel.dk/liburing

#include "yield.hpp"
#include "io_host.hpp"

// In order to be used inside coroutine, `io_coroutine` must be created with new
// It will be deleted automatically when coroutine exits. See `coro_holder::next()`
class io_coroutine {
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

private:
    struct value_holder: nextable {
        int value = INT32_MIN;

        void next(int ret) override {
            value = ret;
        }
    };

    struct coro_holder: nextable {
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
        prepared_operation(io_uring_sqe* sqe, io_coroutine* coro, uint8_t iflags): sqe(sqe), coro(coro) {
            sqe->flags |= iflags;
        }

        int await() {
            coro_holder holder(coro);
            io_uring_sqe_set_data(sqe, &holder);
            coro->coro.yield();
            return coro->coro.current().value();
        }

        [[nodiscard]]
        std::unique_ptr<value_holder> promise() {
            auto holder = std::make_unique<value_holder>();
            io_uring_sqe_set_data(sqe, holder.get());
            return holder;
        }

        void detach() {
            // Ignore result
        }

private:
        io_uring_sqe* sqe;
        io_coroutine* coro;
    };

public:
    [[nodiscard]]
    prepared_operation read(int fd, void *buf, unsigned int nbytes, off_t offset, uint8_t iflags = 0) noexcept {
        auto* sqe = host.io_uring_get_sqe_safe();
        io_uring_prep_read(sqe, fd, buf, nbytes, offset);
        return prepared_operation(sqe, this, iflags);
    }

    [[nodiscard]]
    prepared_operation readv(int fd, const iovec *iovecs, unsigned int nr_vecs, off_t offset, uint8_t iflags = 0) noexcept {
        auto* sqe = host.io_uring_get_sqe_safe();
        io_uring_prep_readv(sqe, fd, iovecs, nr_vecs, offset);
        return prepared_operation(sqe, this, iflags);
    }

    [[nodiscard]]
    prepared_operation write(int fd, const void *buf, unsigned int nbytes, off_t offset, uint8_t iflags = 0) noexcept {
        auto* sqe = host.io_uring_get_sqe_safe();
        io_uring_prep_write(sqe, fd, buf, nbytes, offset);
        return prepared_operation(sqe, this, iflags);
    }

    [[nodiscard]]
    prepared_operation writev(int fd, const iovec *iovecs, unsigned int nr_vecs, off_t offset, uint8_t iflags = 0) noexcept {
        auto* sqe = host.io_uring_get_sqe_safe();
        io_uring_prep_writev(sqe, fd, iovecs, nr_vecs, offset);
        return prepared_operation(sqe, this, iflags);
    }

    [[nodiscard]]
    prepared_operation recv(int fd, void *buf, unsigned int nbytes, int flags, uint8_t iflags = 0) noexcept {
        auto* sqe = host.io_uring_get_sqe_safe();
        io_uring_prep_recv(sqe, fd, buf, nbytes, flags);
        return prepared_operation(sqe, this, iflags);
    }

    [[nodiscard]]
    prepared_operation recvmsg(int sockfd, msghdr *msg, uint32_t flags, uint8_t iflags = 0) noexcept {
        auto* sqe = host.io_uring_get_sqe_safe();
        io_uring_prep_recvmsg(sqe, sockfd, msg, flags);
        return prepared_operation(sqe, this, iflags);
    }

    [[nodiscard]]
    prepared_operation send(int fd, const void *buf, unsigned int nbytes, int flags, uint8_t iflags = 0) noexcept {
        auto* sqe = host.io_uring_get_sqe_safe();
        io_uring_prep_send(sqe, fd, buf, nbytes, flags);
        return prepared_operation(sqe, this, iflags);
    }

    [[nodiscard]]
    prepared_operation sendmsg(int sockfd, const msghdr *msg, uint32_t flags, uint8_t iflags = 0) noexcept {
        auto* sqe = host.io_uring_get_sqe_safe();
        io_uring_prep_sendmsg(sqe, sockfd, msg, flags);
        return prepared_operation(sqe, this, iflags);
    }

    [[nodiscard]]
    prepared_operation poll(int fd, uint32_t pull_masks, uint8_t iflags = 0) noexcept {
        auto* sqe = host.io_uring_get_sqe_safe();
        io_uring_prep_poll_add(sqe, fd, pull_masks);
        return prepared_operation(sqe, this, iflags);
    }

    [[nodiscard]]
    prepared_operation openat(int dfd, const char *path, int flags, mode_t mode, uint8_t iflags = 0) noexcept {
        auto* sqe = host.io_uring_get_sqe_safe();
        io_uring_prep_openat(sqe, dfd, path, flags, mode);
        return prepared_operation(sqe, this, iflags);
    }

    [[nodiscard]]
    prepared_operation accept(int fd, sockaddr *addr, socklen_t *addrlen, int flags, uint8_t iflags = 0) noexcept {
        auto* sqe = host.io_uring_get_sqe_safe();
        io_uring_prep_accept(sqe, fd, addr, addrlen, flags);
        return prepared_operation(sqe, this, iflags);
    }

    [[nodiscard]]
    prepared_operation connect(int fd, sockaddr *addr, socklen_t addrlen, int flags, uint8_t iflags = 0) noexcept {
        auto* sqe = host.io_uring_get_sqe_safe();
        io_uring_prep_connect(sqe, fd, addr, addrlen);
        return prepared_operation(sqe, this, iflags);
    }

    [[nodiscard]]
    prepared_operation nop(uint8_t iflags = 0) noexcept {
        auto* sqe = host.io_uring_get_sqe_safe();
        io_uring_prep_nop(sqe);
        return prepared_operation(sqe, this, iflags);
    }

    [[nodiscard]]
    prepared_operation timeout(__kernel_timespec* ts, uint8_t iflags = 0) noexcept {
        auto* sqe = host.io_uring_get_sqe_safe();
        io_uring_prep_timeout(sqe, ts, 0, 0);
        return prepared_operation(sqe, this, iflags);
    }

    [[nodiscard]]
    prepared_operation statx(int dfd, const char *path, int flags, unsigned mask, struct statx *statxbuf, uint8_t iflags = 0) noexcept {
        auto* sqe = host.io_uring_get_sqe_safe();
        io_uring_prep_statx(sqe, dfd, path, flags, mask, statxbuf);
        return prepared_operation(sqe, this, iflags);
    }

    [[nodiscard]]
    prepared_operation splice(int fd_in, loff_t off_in, int fd_out, loff_t off_out, size_t nbytes, unsigned flags, uint8_t iflags = 0) noexcept {
        auto* sqe = host.io_uring_get_sqe_safe();
        io_uring_prep_splice(sqe, fd_in, off_in, fd_out, off_out, nbytes, flags);
        return prepared_operation(sqe, this, iflags);
    }

    [[nodiscard]]
    prepared_operation tee(int fd_in, int fd_out, size_t nbytes, unsigned flags, uint8_t iflags = 0) noexcept {
        auto* sqe = host.io_uring_get_sqe_safe();
        io_uring_prep_tee(sqe, fd_in, fd_out, nbytes, flags);
        return prepared_operation(sqe, this, iflags);
    }

    [[nodiscard]]
    prepared_operation shutdown(int fd, int how, uint8_t iflags = 0) noexcept {
        auto* sqe = host.io_uring_get_sqe_safe();
        io_uring_prep_shutdown(sqe, fd, how);
        return prepared_operation(sqe, this, iflags);
    }

    [[nodiscard]]
    prepared_operation close(int fd, uint8_t iflags = 0) noexcept {
        auto* sqe = host.io_uring_get_sqe_safe();
        io_uring_prep_close(sqe, fd);
        return prepared_operation(sqe, this, iflags);
    }

    io_host& host;

private:
    FiberSpace::Fiber<int, true> coro;
    std::function<void ()> cleanup;
};
