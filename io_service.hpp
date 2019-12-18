#pragma once
#include <functional>
#include <system_error>
#include <chrono>
#include <sys/poll.h>
#include <sys/timerfd.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <liburing.h>   // http://git.kernel.dk/liburing
#include <execinfo.h>

#include "promise.hpp"
#include "task.hpp"

#ifndef LINUX_KERNEL_VERSION
#   define LINUX_KERNEL_VERSION 55
#endif

/** Helper functions to fill an iovec struct */
constexpr inline iovec to_iov(void *buf, size_t size) noexcept {
    return { buf, size };
}
constexpr inline iovec to_iov(std::string_view sv) noexcept {
    return to_iov(const_cast<char *>(sv.data()), sv.size());
}
template <size_t N>
constexpr inline iovec to_iov(std::array<char, N>& array) noexcept {
    return to_iov(array.data(), array.size());
}

template <typename Fn>
struct on_scope_exit {
    on_scope_exit(Fn &&fn): _fn(std::move(fn)) {}
    ~on_scope_exit() { this->_fn(); }

private:
    Fn _fn;
};

[[nodiscard]]
constexpr inline __kernel_timespec dur2ts(std::chrono::nanoseconds dur) noexcept {
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(dur);
    dur -= secs;
    return { secs.count(), dur.count() };
}

/** Convert errno to exception
 * @throw std::runtime_error / std::system_error
 * @return never
 */
[[noreturn]]
void panic(std::string_view sv, int err) {
#ifndef NDEBUG
    // https://stackoverflow.com/questions/77005/how-to-automatically-generate-a-stacktrace-when-my-program-crashes
    void *array[32];
    size_t size;

    // get void*'s for all entries on the stack
    size = backtrace(array, 32);

    // print out all the frames to stderr
    fprintf(stderr, "Error: errno %d:\n", err);
    backtrace_symbols_fd(array, size, STDERR_FILENO);
#endif

    if (err == EPIPE) {
        throw std::runtime_error("Broken pipe: client socket is closed");
    }
    throw std::system_error(err, std::generic_category(), sv.data());
}

struct panic_on_err {
    panic_on_err(std::string_view _command, bool _use_errno)
        : command(_command)
        , use_errno(_use_errno) {}
    std::string_view command;
    bool use_errno;
};

inline int operator |(int ret, panic_on_err&& poe) {
    if (ret < 0) {
        if (poe.use_errno) {
            panic(poe.command, errno);
        } else {
            if (ret != -ETIME) panic(poe.command, -ret);
        }
    }
    return ret;
}
inline task<int> operator |(task<int> tret, panic_on_err&& poe) {
    co_return (co_await tret) | std::move(poe);
}

/** Get a sqe pointer that can never be NULL
 * @param ring pointer to inited io_uring struct
 * @return pointer to `io_uring_sqe` struct (not NULL)
 */
[[nodiscard]]
io_uring_sqe* io_uring_get_sqe_safe(io_uring *ring) noexcept {
    if (auto* sqe = io_uring_get_sqe(ring)) {
        return sqe;
    } else {
        io_uring_submit(ring);
        sqe = io_uring_get_sqe(ring);
        assert(sqe && "sqe should not be NULL");
        return sqe;
    }
}

class io_service {
public:
    /** Init io_service / io_uring object
     * @see io_uring_setup(2)
     * @param entries Maximum sqe can be gotten without submitting
     * @param flags flags used to init io_uring
     */
    io_service(int entries = 64, unsigned flags = 0) {
        io_uring_queue_init(entries, &ring, flags) | panic_on_err("queue_init", false);
    }

    /** Destroy io_service / io_uring object */
    ~io_service() noexcept {
        io_uring_queue_exit(&ring);
    }

    // io_service is not copyable. It can be moveable but humm...
    io_service(const io_service&) = delete;
    io_service& operator =(const io_service&) = delete;

public:

#define DEFINE_AWAIT_OP(operation)                                                   \
    task<int> operation(                                                             \
        int fd,                                                                      \
        iovec* iovecs,                                                               \
        unsigned nr_vecs,                                                            \
        off_t offset,                                                                \
        uint8_t iflags = 0                                                           \
    ) {                                                                              \
        auto* sqe = io_uring_get_sqe_safe(&ring);                                    \
        io_uring_prep_##operation(sqe, fd, iovecs, nr_vecs, offset);                 \
        return await_work(sqe, iflags, IORING_OP_ASYNC_CANCEL);                      \
    }                                                                                \
                                                                                     \
    task<int> operation(                                                             \
        int fd,                                                                      \
        iovec iov,                                                                   \
        off_t offset,                                                                \
        uint8_t iflags = 0                                                           \
    ) {                                                                              \
        co_return co_await operation(fd, &iov, 1, offset, iflags);                   \
    }

    /** Read data into multiple buffers asynchronously
     * @see preadv2(2)
     * @see io_uring_enter(2) IORING_OP_READV
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     */
    DEFINE_AWAIT_OP(readv)

    /** Write data into multiple buffers asynchronously
     * @see pwrite2(2)
     * @see io_uring_enter(2) IORING_OP_WRITEV
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     */
    DEFINE_AWAIT_OP(writev)
#undef DEFINE_AWAIT_OP

#define DEFINE_AWAIT_OP(operation)                                                   \
    task<int> operation(                                                             \
        int fd,                                                                      \
        void* buf,                                                                   \
        unsigned nbytes,                                                             \
        off_t offset,                                                                \
        int buf_index,                                                               \
        uint8_t iflags = 0                                                           \
    ) {                                                                              \
        auto* sqe = io_uring_get_sqe_safe(&ring);                                    \
        io_uring_prep_##operation(sqe, fd, buf, nbytes, offset, buf_index);          \
        co_return co_await await_work(sqe, iflags, IORING_OP_ASYNC_CANCEL);          \
    }

    /** Read data into a fixed buffer asynchronously
     * @see preadv2(2)
     * @see io_uring_enter(2) IORING_OP_READ_FIXED
     * @param buf_index the index of buffer registered with register_buffers
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     */
    DEFINE_AWAIT_OP(read_fixed)

    /** Write data into a fixed buffer asynchronously
     * @see pwritev2(2)
     * @see io_uring_enter(2) IORING_OP_WRITE_FIXED
     * @param buf_index the index of buffer registered with register_buffers
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     */
    DEFINE_AWAIT_OP(write_fixed)
#undef DEFINE_AWAIT_OP

    /** Synchronize a file's in-core state with storage device asynchronously
     * @see fsync(2)
     * @see io_uring_enter(2) IORING_OP_FSYNC
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     */
    task<int> fsync(
        int fd,
        unsigned fsync_flags,
        uint8_t iflags = 0
    ) {
        auto* sqe = io_uring_get_sqe_safe(&ring);
        io_uring_prep_fsync(sqe, fd, fsync_flags);
        return await_work(sqe, iflags, IORING_OP_ASYNC_CANCEL);
    }

    /** Sync a file segment with disk asynchronously
     * @see sync_file_range(2)
     * @see io_uring_enter(2) IORING_OP_SYNC_FILE_RANGE
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     */
    task<int> sync_file_range(
        int fd,
        off64_t offset,
        off64_t nbytes,
        unsigned sync_range_flags,
        uint8_t iflags = 0
    ) {
        auto* sqe = io_uring_get_sqe_safe(&ring);
        io_uring_prep_rw(IORING_OP_SYNC_FILE_RANGE, sqe, fd, nullptr, nbytes, offset);
        sqe->sync_range_flags = sync_range_flags;
        return await_work(sqe, iflags, IORING_OP_ASYNC_CANCEL);
    }

#define DEFINE_AWAIT_OP(operation)                                                   \
    task<int> operation(                                                             \
        int sockfd,                                                                  \
        msghdr* msg,                                                                 \
        uint32_t flags,                                                              \
        uint8_t iflags = 0                                                           \
    ) {                                                                              \
        auto* sqe = io_uring_get_sqe_safe(&ring);                                    \
        io_uring_prep_##operation(sqe, sockfd, msg, flags);                          \
        return await_work(sqe, iflags, IORING_OP_ASYNC_CANCEL);                      \
    }                                                                                \
                                                                                     \
    task<int> operation(                                                             \
        int sockfd,                                                                  \
        iovec iov,                                                                   \
        uint32_t flags,                                                              \
        uint8_t iflags = 0                                                           \
    ) {                                                                              \
        msghdr msg = { .msg_iov = &iov, .msg_iovlen = 1 };                           \
        co_return co_await operation(sockfd, &msg, flags, iflags);                   \
    }

    /** Receive a message from a socket asynchronously
     * @see recvmsg(2)
     * @see io_uring_enter(2) IORING_OP_RECVMSG
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     */
    DEFINE_AWAIT_OP(recvmsg)

    /** Send a message on a socket asynchronously
     * @see sendmsg(2)
     * @see io_uring_enter(2) IORING_OP_SENDMSG
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     */
    DEFINE_AWAIT_OP(sendmsg)
#undef DEFINE_AWAIT_OP

    /** Wait for an event on a file descriptor asynchronously
     * @see poll(2)
     * @see io_uring_enter(2)
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     */
    task<int> poll(
        int fd,
        short poll_mask,
        uint8_t iflags = 0
    ) {
        auto* sqe = io_uring_get_sqe_safe(&ring);
        io_uring_prep_poll_add(sqe, fd, poll_mask);
        return await_work(sqe, iflags, IORING_OP_POLL_REMOVE);
    }

    /** Enqueue a NOOP command, which eventually acts like pthread_yield when awaiting
     * @see io_uring_enter(2) IORING_OP_NOP
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     */
    task<int> yield(
        uint8_t iflags = 0
    ) {
        auto* sqe = io_uring_get_sqe_safe(&ring);
        io_uring_prep_nop(sqe);
        return await_work(sqe, iflags, IORING_OP_ASYNC_CANCEL);
    }

    /** Accept a connection on a socket asynchronously
     * @see accept4(2)
     * @see io_uring_enter(2) IORING_OP_ACCEPT
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     */
    task<int> accept(
        int fd,
        sockaddr *addr,
        socklen_t *addrlen,
        int flags = 0,
        uint8_t iflags = 0
    ) {
#if LINUX_KERNEL_VERSION >= 55
        auto* sqe = io_uring_get_sqe_safe(&ring);
        io_uring_prep_accept(sqe, fd, addr, addrlen, flags);
        return await_work(sqe, iflags, IORING_OP_ASYNC_CANCEL);
#else
        co_await poll(fd, POLLIN, iflags);
        co_return ::accept4(fd, addr, addrlen, flags);
#endif
    }

    /** Initiate a connection on a socket asynchronously
     * @see connect(2)
     * @see io_uring_enter(2) IORING_OP_CONNECT
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     */
    task<int> connect(
        int fd,
        sockaddr *addr,
        socklen_t addrlen,
        int flags = 0,
        uint8_t iflags = 0
    ) {
#if LINUX_KERNEL_VERSION >= 55
        auto* sqe = io_uring_get_sqe_safe(&ring);
        io_uring_prep_connect(sqe, fd, addr, addrlen);
        return await_work(sqe, iflags, IORING_OP_ASYNC_CANCEL);
#else
        co_await poll(fd, POLLIN, iflags);
        co_return ::connect(fd, addr, addrlen);
#endif
    }

    /** Wait for specified duration asynchronously
     * @see io_uring_enter(2) IORING_OP_TIMEOUT
     * @param ts initial expiration, timespec
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     */
    task<int> delay(
        __kernel_timespec ts,
        uint8_t iflags = 0
    ) {
#if LINUX_KERNEL_VERSION >= 55
        auto* sqe = io_uring_get_sqe_safe(&ring);
        // Everytime we pass pointers into other function,
        // we MUST use co_return co_await to insure that variable
        // isn't destructed before await_work truly returns
        io_uring_prep_timeout(sqe, &ts, 0, 0);
        // IORING_OP_TIMEOUT_REMOVE is supported in Linux 5.5+ only, not in Linux 5.4.x
        co_return co_await await_work(sqe, iflags, IORING_OP_TIMEOUT_REMOVE);
#else
        itimerspec exp = { {}, { ts.tv_sec, ts.tv_nsec } };
        // IOSQE_IO_LINK won't work here because the timer is created before the fd is polled
        auto tfd = timerfd_create(CLOCK_MONOTONIC, 0);
        if (tfd < 0) co_return -errno;
        on_scope_exit closefd([=]() { close(tfd); });
        if (timerfd_settime(tfd, 0, &exp, nullptr) < 0) co_return -errno;
        // Insure that tfd is NOT closed before poll is truly finished
        // IOSQE_IO_LINK doesn't work here since `timerfd_settime` is called before polling
        co_return co_await poll(tfd, POLLIN, iflags);
#endif
    }

    task<int> delay(
        std::chrono::nanoseconds dur,
        uint8_t iflags = 0
    ) {
        return delay(dur2ts(dur), iflags);
    }

    task<int> openat(
        int dfd,
        const char *path,
        int flags,
        mode_t mode,
        uint8_t iflags = 0
    ) {
#if LINUX_KERNEL_VERSION >= 56
        auto* sqe = io_uring_get_sqe_safe(&ring);
        io_uring_prep_openat(sqe, dfd, path, flags, mode);
        return await_work(sqe, iflags, 0);
#else
        co_return ::openat(dfd, path, flags, mode);
#endif
    }

    task<int> close(
        int fd,
        uint8_t iflags = 0
    ) {
#if LINUX_KERNEL_VERSION >= 56
        auto* sqe = io_uring_get_sqe_safe(&ring);
        io_uring_prep_close(sqe, fd);
        return await_work(sqe, iflags, 0);
#else
        co_return ::close(fd);
#endif
    }

    task<int> statx(
        int dfd,
        const char *path,
        int flags,
        unsigned mask,
        struct statx *statxbuf,
        uint8_t iflags = 0
    ) {
#if LINUX_KERNEL_VERSION >= 56
        auto* sqe = io_uring_get_sqe_safe(&ring);
        io_uring_prep_statx(sqe, dfd, path, flags, mask, statxbuf);
        return await_work(sqe, iflags, 0);
#else
        co_return ::statx(dfd, path, flags, mask, statxbuf);
#endif
    }

private:
    task<int> await_work(
        io_uring_sqe* sqe,
        uint8_t iflags,
        int cancel_opcode
    ) {
        promise<int> p([cancel_opcode = cancel_opcode, pring = &ring] (promise<int>* self) {
            // For Linux 5.4 and below, canceled operations won't return -ECANCELED, thus no exceptions will be thrown
            io_uring_sqe *sqe = io_uring_get_sqe_safe(pring);
            io_uring_prep_rw(cancel_opcode, sqe, -1, self, 0, 0);
        });
        io_uring_sqe_set_flags(sqe, iflags);
        io_uring_sqe_set_data(sqe, &p);
        co_return co_await p;
    }

public:
    /** Wait for an event forever, blocking
     * @see io_uring_wait_cqe
     * @see io_uring_enter(2)
     * @return a pair of promise pointer (used for resuming suspended coroutine) and retcode of finished command
     */
    [[nodiscard]]
    std::pair<promise<int> *, int> wait_event() {
        io_uring_cqe* cqe;
        promise<int>* coro;
        io_uring_submit(&ring);

        do {
            io_uring_wait_cqe(&ring, &cqe) | panic_on_err("wait_cqe", false);
            io_uring_cqe_seen(&ring, cqe);
            coro = static_cast<promise<int> *>(io_uring_cqe_get_data(cqe));
        } while (coro == nullptr);

        return { coro, cqe->res };
    }

public:
    /** Peek an event, if any
     * @see io_uring_peek_cqe
     * @return a pair of promise pointer (used for resuming suspended coroutine) and retcode of finished command
     * @return optional. std::nullopt if no events are ready
     */
    [[nodiscard]]
    std::optional<std::pair<promise<int> *, int>> peek_event() {
        for (io_uring_cqe* cqe; io_uring_peek_cqe(&ring, &cqe) >= 0 && cqe; ) {
            io_uring_cqe_seen(&ring, cqe);

            if (auto* coro = static_cast<promise<int> *>(io_uring_cqe_get_data(cqe))) {
                return std::make_pair(coro, cqe->res);
            }
        }
        return std::nullopt;
    }

    /** Wait for an event, with timeout
     * @see io_uring_wait_cqe_timeout
     * @return a pair of promise pointer (used for resuming suspended coroutine) and retcode of finished command
     * @return optional. std::nullopt if no events are ready
     */
    [[nodiscard]]
    std::optional<std::pair<promise<int> *, int>> timedwait_event(__kernel_timespec timeout) {
        if  (auto result = peek_event()) return result;
        if (io_uring_cqe* cqe; io_uring_wait_cqe_timeout(&ring, &cqe, &timeout) >= 0 && cqe) {
            io_uring_cqe_seen(&ring, cqe);

            if (auto* coro = static_cast<promise<int> *>(io_uring_cqe_get_data(cqe))) {
                return std::make_pair(coro, cqe->res);
            }
            return peek_event();
        }
        return std::nullopt;
    }

    [[nodiscard]]
    inline std::optional<std::pair<promise<int> *, int>> timedwait_event(std::chrono::nanoseconds dur) {
        return timedwait_event(dur2ts(dur));
    }

public:
    /** Register files for I/O
     * @param fds fds to register
     * @see io_uring_register(2) IORING_REGISTER_FILES
     */
    void register_files(std::initializer_list<int> fds) {
        io_uring_register_files(&ring, fds.begin(), fds.size()) | panic_on_err("io_uring_register_files", false);
    }

    /** Unregister all files
     * @see io_uring_register(2) IORING_UNREGISTER_FILES
     */
    int unregister_files() {
        return io_uring_unregister_files(&ring);
    }

public:
    /** Register buffers for I/O
     * @param ioves array of iovec to register
     * @see io_uring_register(2) IORING_REGISTER_BUFFERS
     */
    template <unsigned int N>
    void register_buffers(iovec (&&ioves) [N]) {
        io_uring_register_buffers(&ring, &ioves[0], N) | panic_on_err("io_uring_register_buffers", false);
    }

    /** Unregister all buffers
     * @see io_uring_register(2) IORING_UNREGISTER_BUFFERS
     */
    int unregister_buffers() {
        return io_uring_unregister_buffers(&ring);
    }

public:
    /** Return internal io_uring handle */
    [[nodiscard]]
    io_uring& get_handle() noexcept {
        return ring;
    }

private:
    io_uring ring;
};
