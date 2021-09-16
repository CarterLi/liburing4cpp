#pragma once
#include <functional>
#include <liburing.h>   // http://git.kernel.dk/liburing

#include "yield.hpp"

// 填充 iovec 结构体
constexpr inline iovec to_iov(void *buf, size_t size) {
    return { buf, size };
}
constexpr inline iovec to_iov(std::string_view sv) {
    return to_iov(const_cast<char *>(sv.data()), sv.size());
}
template <size_t N>
constexpr inline iovec to_iov(std::array<char, N>& array) {
    return to_iov(array.data(), array.size());
}

template <typename Fn>
struct on_scope_exit {
    on_scope_exit(Fn &&fn): _fn(std::move(fn)) {}
    ~on_scope_exit() { this->_fn(); }

private:
    Fn _fn;
};

[[noreturn]]
void panic(std::string_view sv, int err = 0) { // 简单起见，把错误直接转化成异常抛出，终止程序
    if (err == 0) err = errno;
    fprintf(stderr, "errno: %d\n", err);
    if (err == EPIPE) {
        throw std::runtime_error("Broken pipe: client socket is closed");
    }
    throw std::system_error(err, std::generic_category(), sv.data());
}

class io_service: public FiberSpace::Fiber<int, std::function<void ()>> {
public:
    using BaseType = FiberSpace::Fiber<int, std::function<void ()>>;
    using FuncType = BaseType::FuncType;

public:
    static int runningCoroutines;
    static io_uring ring;

    explicit io_service(FuncType f): BaseType(std::move(f)) {
        ++io_service::runningCoroutines;
        this->next(); // We have to make sure that this->yield() is executed at least once.
    }

    explicit io_service(FuncType f, std::function<void ()> cleanup): BaseType(std::move(f)) {
        this->localData = std::move(cleanup);
        ++io_service::runningCoroutines;
        this->next();
    }

    ~io_service() {
        --io_service::runningCoroutines;
        if (this->localData) this->localData();
    }

public:
    int await_read(int fd, void *buf, unsigned int nbytes, off_t offset, uint8_t iflags = 0) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_read(sqe, fd, buf, nbytes, offset);
        return await_work(sqe, "read", iflags);
    }

    int await_readv(int fd, const iovec *iovecs, unsigned int nr_vecs, off_t offset, uint8_t iflags = 0) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_readv(sqe, fd, iovecs, nr_vecs, offset);
        return await_work(sqe, "readv", iflags);
    }

    int await_write(int fd, const void *buf, unsigned int nbytes, off_t offset, uint8_t iflags = 0) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_write(sqe, fd, buf, nbytes, offset);
        return await_work(sqe, "write", iflags);
    }

    int await_writev(int fd, const iovec *iovecs, unsigned int nr_vecs, off_t offset, uint8_t iflags = 0) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_writev(sqe, fd, iovecs, nr_vecs, offset);
        return await_work(sqe, "writev", iflags);
    }

    int await_recv(int fd, void *buf, unsigned int nbytes, int flags, uint8_t iflags = 0) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_recv(sqe, fd, buf, nbytes, flags);
        return await_work(sqe, "recv", iflags);
    }

    int await_recvmsg(int sockfd, msghdr *msg, uint32_t flags, uint8_t iflags = 0) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_recvmsg(sqe, sockfd, msg, flags);
        return await_work(sqe, "recvmsg", iflags);
    }

    int await_send(int fd, const void *buf, unsigned int nbytes, int flags, uint8_t iflags = 0) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_send(sqe, fd, buf, nbytes, flags);
        return await_work(sqe, "send", iflags);
    }

    int await_sendmsg(int sockfd, const msghdr *msg, uint32_t flags, uint8_t iflags = 0) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_sendmsg(sqe, sockfd, msg, flags);
        return await_work(sqe, "sendmsg", iflags);
    }

    int await_poll(int fd, uint32_t pull_flags, uint8_t iflags = 0) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_poll_add(sqe, fd, pull_flags);
        return await_work(sqe, "poll", iflags);
    }

    int await_openat(int dfd, const char *path, int flags, mode_t mode, uint8_t iflags = 0) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_openat(sqe, dfd, path, flags, mode);
        return await_work(sqe, "openat", iflags);
    }

    int await_accept(int fd, sockaddr *addr, socklen_t *addrlen, int flags, uint8_t iflags = 0) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_accept(sqe, fd, addr, addrlen, flags);
        return await_work(sqe, "accept", iflags);
    }

    int await_nop(uint8_t iflags = 0) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_nop(sqe);
        return await_work(sqe, "nop", iflags);
    }

    int await_timeout(const __kernel_timespec* ts, uint8_t iflags = 0) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_timeout(sqe, const_cast<__kernel_timespec *>(ts), 0, 0);
        return await_work(sqe, "timeout", iflags);
    }

    int await_statx(int dfd, const char *path, int flags, unsigned mask, struct statx *statxbuf, uint8_t iflags = 0) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_statx(sqe, dfd, path, flags, mask, statxbuf);
        return await_work(sqe, "statx", iflags);
    }

    int await_splice(int fd_in, loff_t off_in, int fd_out, loff_t off_out, size_t nbytes, unsigned flags, uint8_t iflags = 0) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_splice(sqe, fd_in, off_in, fd_out, off_out, nbytes, flags);
        return await_work(sqe, "splice", iflags);
    }

    int await_tee(int fd_in, int fd_out, size_t nbytes, unsigned flags, uint8_t iflags = 0) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_tee(sqe, fd_in, fd_out, nbytes, flags);
        return await_work(sqe, "tee", iflags);
    }

    int await_shutdown(int fd, int how, unsigned flags, uint8_t iflags = 0) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_shutdown(sqe, fd, how);
        return await_work(sqe, "shutdown", iflags);
    }

public:
    int await_work(io_uring_sqe* sqe, std::string_view op, uint8_t iflags) noexcept {
        io_uring_sqe_set_data(sqe, this);
        sqe->flags |= iflags;
        this->yield();
        int res = this->current().value();
#ifndef NDEBUG
        if (res < 0 && res != -ETIME) fprintf(stderr, "%.*s: %d\n", (int)op.length(), op.data(), -res);
#endif
        return res;
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
        while (true) {
            io_uring_submit_and_wait(&ring, 1);

            io_uring_cqe *cqe;
            unsigned head;

            io_uring_for_each_cqe(&ring, head, cqe) {
                ++cqe_count;
                auto coro = static_cast<io_service *>(io_uring_cqe_get_data(cqe));
                if (!coro->next(cqe->res)) delete coro;
            }

#ifndef NDEBUG
            printf(__FILE__ ": Found %u cqe(s), looping...\n", cqe_count);
#endif
            io_uring_cq_advance(&ring, cqe_count);
            cqe_count = 0;
        }
    }

private:
    int cqe_count = 0;
};
int io_service::runningCoroutines = 0;
io_uring io_service::ring;
