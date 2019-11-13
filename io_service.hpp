#pragma once
#include <functional>
#include <system_error>
#include <chrono>
#include <sys/timerfd.h>
#include <unistd.h>
#include <liburing.h>   // http://git.kernel.dk/liburing
#include <sys/poll.h>

#include "task.hpp"

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
void panic(std::string_view sv, int err = 0) {
    if (err == 0) err = errno;
    fprintf(stderr, "errno: %d\n", err);
    if (err == EPIPE) {
        throw std::runtime_error("Broken pipe: client socket is closed");
    }
    throw std::system_error(err, std::generic_category(), sv.data());
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

// This cannot be an inlined function, or `stack-use-after-scope` happens
// forceinline won't work too
#define AWAIT_WORK(sqe, iflags, command) \
do { \
    promise<int> p; \
    io_uring_sqe_set_flags(sqe, iflags); \
    io_uring_sqe_set_data(sqe, &p); \
    int res = co_await p; \
    if (res < 0) panic(command, -res); \
    co_return res; \
} while (false)

class io_service {
public:
    /** Init io_service / io_uring object
     * @see io_uring_setup(2)
     * @param entries Maximum sqe can be gotten without submitting
     * @param flags flags used to init io_uring
     */
    io_service(int entries = 64, unsigned flags = 0) {
        if (io_uring_queue_init(entries, &ring, flags)) panic("queue_init");
    }

    /** Destroy io_service / io_uring object */
    ~io_service() noexcept {
        io_uring_queue_exit(&ring);
    }

    // io_service is not copyable. It can be moveable but humm...
    io_service(const io_service&) = delete;
    io_service& operator =(const io_service&) = delete;

public:

#define DEFINE_AWAIT_OP(operation) \
    template <unsigned int N> \
    [[nodiscard]] \
    task<int> operation ( \
        int fd, \
        iovec (&&ioves) [N], \
        off_t offset, \
        uint8_t iflags = 0, \
        std::string_view command = #operation \
    ) { \
        auto* sqe = io_uring_get_sqe_safe(&ring); \
        io_uring_prep_##operation(sqe, fd, ioves, N, offset); \
        AWAIT_WORK(sqe, iflags, command); \
    }

    /** Read data into multiple buffers asynchronously
     * @see preadv2(2)
     * @see io_uring_enter(2) IORING_OP_READV
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     * @warning do NOT discard the returned object
     */
    DEFINE_AWAIT_OP(readv)

    /** Write data into multiple buffers asynchronously
     * @see pwrite2(2)
     * @see io_uring_enter(2) IORING_OP_WRITEV
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     * @warning do NOT discard the returned object
     */
    DEFINE_AWAIT_OP(writev)
#undef DEFINE_AWAIT_OP

#define DEFINE_AWAIT_OP(operation) \
    [[nodiscard]] \
    task<int> operation ( \
        int fd, \
        void* buf, \
        unsigned nbytes, \
        off_t offset, \
        int buf_index, \
        uint8_t iflags = 0, \
        std::string_view command = #operation \
    ) { \
        auto* sqe = io_uring_get_sqe_safe(&ring); \
        io_uring_prep_##operation(sqe, fd, buf, nbytes, offset, buf_index); \
        AWAIT_WORK(sqe, iflags, command); \
    }

    /** Read data into a fixed buffer asynchronously
     * @see preadv2(2)
     * @see io_uring_enter(2) IORING_OP_READ_FIXED
     * @param buf_index the index of buffer registered with register_buffers
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     * @warning do NOT discard the returned object
     */
    DEFINE_AWAIT_OP(read_fixed)

    /** Write data into a fixed buffer asynchronously
     * @see pwritev2(2)
     * @see io_uring_enter(2) IORING_OP_WRITE_FIXED
     * @param buf_index the index of buffer registered with register_buffers
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     * @warning do NOT discard the returned object
     */
    DEFINE_AWAIT_OP(write_fixed)
#undef DEFINE_AWAIT_OP

#define DEFINE_AWAIT_OP(operation) \
    template <unsigned int N> \
    [[nodiscard]] \
    task<int> operation( \
        int sockfd, \
        iovec (&&ioves) [N], \
        uint32_t flags, \
        uint8_t iflags = 0, \
        std::string_view command = #operation \
    ) { \
        msghdr msg = { \
            .msg_iov = ioves, \
            .msg_iovlen = N, \
        }; \
        auto* sqe = io_uring_get_sqe_safe(&ring); \
        io_uring_prep_##operation(sqe, sockfd, &msg, flags); \
        AWAIT_WORK(sqe, iflags, command); \
    }

    /** Receive a message from a socket asynchronously
     * @see recvmsg(2)
     * @see io_uring_enter(2) IORING_OP_RECVMSG
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     * @warning do NOT discard the returned object
     */
    DEFINE_AWAIT_OP(recvmsg)

    /** Send a message on a socket asynchronously
     * @see sendmsg(2)
     * @see io_uring_enter(2) IORING_OP_SENDMSG
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     * @warning do NOT discard the returned object
     */
    DEFINE_AWAIT_OP(sendmsg)
#undef DEFINE_AWAIT_OP

    /** Wait for an event on a file descriptor asynchronously
     * @see poll(2)
     * @see io_uring_enter(2)
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     * @warning do NOT discard the returned object
     */
    [[nodiscard]]
    task<int> poll(
        int fd,
        short poll_mask,
        uint8_t iflags = 0,
        std::string_view command = "poll"
    ) {
        auto* sqe = io_uring_get_sqe_safe(&ring);
        io_uring_prep_poll_add(sqe, fd, poll_mask);
        AWAIT_WORK(sqe, iflags, command);
    }

    /** Enqueue a NOOP command, which eventually acts like pthread_yield when awaiting
     * @see io_uring_enter(2) IORING_OP_NOP
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     * @warning do NOT discard the returned object
     */
    [[nodiscard]]
    task<int> yield(
        uint8_t iflags = 0,
        std::string_view command = "yield"
    ) {
        auto* sqe = io_uring_get_sqe_safe(&ring);
        io_uring_prep_nop(sqe);
        AWAIT_WORK(sqe, iflags, command);
    }

#if defined(USE_NEW_IO_URING_FEATURES)
    /** Accept a connection on a socket asynchronously
     * @see accept4(2)
     * @see io_uring_enter(2) IORING_OP_ACCEPT
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     * @warning do NOT discard the returned object
     */
    [[nodiscard]]
    task<int> accept(
        int fd,
        sockaddr *addr,
        socklen_t *addrlen,
        int flags = 0,
        uint8_t iflags = 0,
        std::string_view command = "accept"
    ) {
        auto* sqe = io_uring_get_sqe_safe(&ring);
        io_uring_prep_accept(sqe, fd, addr, addrlen, flags);
        AWAIT_WORK(sqe, iflags, command);
    }

    /** Wait for specified duration asynchronously
     * @see io_uring_enter(2) IORING_OP_TIMEOUT
     * @param ts initial expiration, timespec
     * @param iflags IOSQE_* flags
     * @param command text will be thrown when fail
     * @return a task object for awaiting
     * @warning do NOT discard the returned object
     */
    [[nodiscard]]
    task<int> delay(
        __kernel_timespec ts,
        uint8_t iflags = 0,
        std::string_view command = "delay"
    ) {
        auto* sqe = io_uring_get_sqe_safe(&ring);
        io_uring_prep_timeout(sqe, &ts, 0, 0);
        promise<int> p;
        io_uring_sqe_set_flags(sqe, iflags);
        io_uring_sqe_set_data(sqe, &p);
        int res = co_await p;
        if (res < 0 && res != -ETIME) panic(command, -res);
        co_return res;
    }
#else
    [[nodiscard]]
    task<int> accept(
        int fd,
        sockaddr *addr,
        socklen_t *addrlen,
        int flags = 0,
        uint8_t iflags = 0,
        std::string_view command = "accept"
    ) {
        co_await poll(fd, POLLIN, iflags, command);
        co_return accept4(fd, addr, addrlen, flags);
    }

    [[nodiscard]]
    task<int> delay(
        __kernel_timespec ts,
        uint8_t iflags = 0, // IOSQE_IO_LINK doesn't work here since `timerfd_settime` is called before polling
        std::string_view command = "delay"
    ) {
        itimerspec exp = { {}, { ts.tv_sec, ts.tv_nsec } };
        auto tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
        if (timerfd_settime(tfd, 0, &exp, nullptr)) panic("timerfd");
        on_scope_exit closefd([=]() { close(tfd); });
        // Cannot return poll directly or closefd will be called early incorrectly
        // which results in bad file discriptor exception
        co_await poll(tfd, POLLIN, iflags, command);
        printf("%d\n", (int)ts.tv_sec);
        co_return 0;
    }
#endif

    [[nodiscard]]
    task<int> delay(
        std::chrono::nanoseconds dur,
        uint8_t iflags = 0,
        std::string_view command = "delay"
    ) {
        return delay(dur2ts(dur), iflags, command);
    }
#undef AWAIT_WORK

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
            if (io_uring_wait_cqe(&ring, &cqe)) panic("wait_cqe");
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
        io_uring_cqe* cqe;
        while (io_uring_peek_cqe(&ring, &cqe) >= 0 && cqe) {
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
     * @require Linux 5.4+
     */
    [[nodiscard]]
    std::optional<std::pair<promise<int> *, int>> timedwait_event(__kernel_timespec timeout) {
        if  (auto result = peek_event()) return result;

        io_uring_cqe* cqe;
        while (io_uring_wait_cqe_timeout(&ring, &cqe, &timeout) >= 0 && cqe) {
            io_uring_cqe_seen(&ring, cqe);

            if (auto* coro = static_cast<promise<int> *>(io_uring_cqe_get_data(cqe))) {
                return std::make_pair(coro, cqe->res);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]]
    std::optional<std::pair<promise<int> *, int>> timedwait_event(std::chrono::nanoseconds dur) {
        return timedwait_event(dur2ts(dur));
    }

public:
    /** Register files for I/O
     * @param fds fds to register
     * @see io_uring_register(2) IORING_REGISTER_FILES
     */
    void register_files(std::initializer_list<int> fds) {
        if (io_uring_register_files(&ring, fds.begin(), fds.size()) < 0) panic("io_uring_register_files");
    }

    /** Unregister all files
     * @see io_uring_register(2) IORING_UNREGISTER_FILES
     */
    void unregister_files() {
        if (io_uring_unregister_files(&ring) < 0) panic("io_uring_unregister_files");
    }

public:
    /** Register buffers for I/O
     * @param ioves array of iovec to register
     * @see io_uring_register(2) IORING_REGISTER_BUFFERS
     */
    template <unsigned int N>
    void register_buffers(iovec (&&ioves) [N]) {
        if (io_uring_register_buffers(&ring, &ioves[0], N)) panic("io_uring_register_buffers");
    }

    /** Unregister all buffers
     * @see io_uring_register(2) IORING_UNREGISTER_BUFFERS
     */
    void unregister_buffers() {
        if (io_uring_unregister_buffers(&ring) < 0) panic("io_uring_unregister_buffers");
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
