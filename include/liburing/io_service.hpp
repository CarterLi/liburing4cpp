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
#ifndef NDEBUG
#   include <execinfo.h>
#endif

#include <liburing/sqe_awaitable.hpp>
#include <liburing/task.hpp>
#include <liburing/utils.hpp>

#ifdef LIBURING_VERBOSE
#   define puts_if_verbose(x) puts(x)
#   define printf_if_verbose(...) printf(__VA_ARGS__)
#else
#   define puts_if_verbose(x) 0
#   define printf_if_verbose(...) 0
#endif

namespace uio {
class io_service {
public:
    /** Init io_service / io_uring object
     * @see io_uring_setup(2)
     * @param entries Maximum sqe can be gotten without submitting
     * @param flags flags used to init io_uring
     * @param wq_fd existing io_uring ring_fd used by IORING_SETUP_ATTACH_WQ
     * @note io_service is NOT thread safe, nor is liburing. When used in a
     *       multi-threaded program, it's highly recommended to create
     *       io_service/io_uring instance per thread, and set IORING_SETUP_ATTACH_WQ
     *       flag to make sure that kernel shares the only async worker thread pool.
     *       See `IORING_SETUP_ATTACH_WQ` for detail.
     */
    io_service(int entries = 64, uint32_t flags = 0, uint32_t wq_fd = 0) {
        io_uring_params p = {
            .flags = flags,
            .wq_fd = wq_fd,
        };

        io_uring_queue_init_params(entries, &ring, &p) | panic_on_err("queue_init_params", false);

        auto* probe = io_uring_get_probe_ring(&ring);
        on_scope_exit free_probe([=]() { io_uring_free_probe(probe); });
#define TEST_IORING_OP(opcode) do {\
    for (int i = 0; i < probe->ops_len; ++i) {\
        if (probe->ops[i].op == opcode && probe->ops[i].flags & IO_URING_OP_SUPPORTED) {\
                probe_ops[i] = true;\
                puts_if_verbose("\t" #opcode);\
                break;\
            }\
        }\
    } while (0)
    puts_if_verbose("Supported io_uring opcodes by current kernel:");
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
    TEST_IORING_OP(IORING_OP_MSG_RING);
    TEST_IORING_OP(IORING_OP_FSETXATTR);
    TEST_IORING_OP(IORING_OP_SETXATTR);
    TEST_IORING_OP(IORING_OP_FGETXATTR);
    TEST_IORING_OP(IORING_OP_GETXATTR);
    TEST_IORING_OP(IORING_OP_SOCKET);
    TEST_IORING_OP(IORING_OP_URING_CMD);
    TEST_IORING_OP(IORING_OP_SEND_ZC);
    TEST_IORING_OP(IORING_OP_SENDMSG_ZC);
#undef TEST_IORING_OP

#define TEST_IORING_FEATURE(feature) if (p.features & feature) puts_if_verbose("\t" #feature)
    puts_if_verbose("Supported io_uring features by current kernel:");
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
    TEST_IORING_FEATURE(IORING_FEAT_CQE_SKIP);
    TEST_IORING_FEATURE(IORING_FEAT_LINKED_FILE);
    TEST_IORING_FEATURE(IORING_FEAT_REG_REG_RING);
#undef TEST_IORING_FEATURE
    }

    /** Destroy io_service / io_uring object */
    ~io_service() noexcept {
        io_uring_queue_exit(&ring);
    }

    // io_service is not copyable. It can be moveable but humm...
    io_service(const io_service&) = delete;
    io_service& operator =(const io_service&) = delete;

public:

    /** Read data into multiple buffers asynchronously
     * @see preadv2(2)
     * @see io_uring_enter(2) IORING_OP_READV
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable readv(
        int fd,
        const iovec* iovecs,
        unsigned nr_vecs,
        off_t offset,
        uint8_t iflags = 0
    ) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_readv(sqe, fd, iovecs, nr_vecs, offset);
        return await_work(sqe, iflags);
    }

    sqe_awaitable readv2(
        int fd,
        const iovec* iovecs,
        unsigned nr_vecs,
        off_t offset,
        int flags,
        uint8_t iflags = 0
    ) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_readv2(sqe, fd, iovecs, nr_vecs, offset, flags);
        return await_work(sqe, iflags);
    }

    /** Write data into multiple buffers asynchronously
     * @see pwritev2(2)
     * @see io_uring_enter(2) IORING_OP_WRITEV
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable writev(
        int fd,
        const iovec* iovecs,
        unsigned nr_vecs,
        off_t offset,
        uint8_t iflags = 0
    ) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_writev(sqe, fd, iovecs, nr_vecs, offset);
        return await_work(sqe, iflags);
    }

    sqe_awaitable writev2(
        int fd,
        const iovec* iovecs,
        unsigned nr_vecs,
        off_t offset,
        int flags,
        uint8_t iflags = 0
    ) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_writev2(sqe, fd, iovecs, nr_vecs, offset, flags);
        return await_work(sqe, iflags);
    }

    /** Read from a file descriptor at a given offset asynchronously
     * @see pread(2)
     * @see io_uring_enter(2) IORING_OP_READ
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable read(
        int fd,
        void* buf,
        unsigned nbytes,
        off_t offset,
        uint8_t iflags = 0
    ) {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_read(sqe, fd, buf, nbytes, offset);
        return await_work(sqe, iflags);
    }

    /** Write to a file descriptor at a given offset asynchronously
     * @see pwrite(2)
     * @see io_uring_enter(2) IORING_OP_WRITE
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable write(
        int fd,
        const void* buf,
        unsigned nbytes,
        off_t offset,
        uint8_t iflags = 0
    ) {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_write(sqe, fd, buf, nbytes, offset);
        return await_work(sqe, iflags);
    }

    /** Read data into a fixed buffer asynchronously
     * @see preadv2(2)
     * @see io_uring_enter(2) IORING_OP_READ_FIXED
     * @param buf_index the index of buffer registered with register_buffers
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable read_fixed(
        int fd,
        void* buf,
        unsigned nbytes,
        off_t offset,
        int buf_index,
        uint8_t iflags = 0
    ) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_read_fixed(sqe, fd, buf, nbytes, offset, buf_index);
        return await_work(sqe, iflags);
    }

    /** Write data into a fixed buffer asynchronously
     * @see pwritev2(2)
     * @see io_uring_enter(2) IORING_OP_WRITE_FIXED
     * @param buf_index the index of buffer registered with register_buffers
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable write_fixed(
        int fd,
        const void* buf,
        unsigned nbytes,
        off_t offset,
        int buf_index,
        uint8_t iflags = 0
    ) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_write_fixed(sqe, fd, buf, nbytes, offset, buf_index);
        return await_work(sqe, iflags);
    }

    /** Synchronize a file's in-core state with storage device asynchronously
     * @see fsync(2)
     * @see io_uring_enter(2) IORING_OP_FSYNC
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable fsync(
        int fd,
        unsigned fsync_flags,
        uint8_t iflags = 0
    ) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_fsync(sqe, fd, fsync_flags);
        return await_work(sqe, iflags);
    }

    /** Sync a file segment with disk asynchronously
     * @see sync_file_range(2)
     * @see io_uring_enter(2) IORING_OP_SYNC_FILE_RANGE
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable sync_file_range(
        int fd,
        off64_t offset,
        off64_t nbytes,
        unsigned sync_range_flags,
        uint8_t iflags = 0
    ) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_rw(IORING_OP_SYNC_FILE_RANGE, sqe, fd, nullptr, nbytes, offset);
        sqe->sync_range_flags = sync_range_flags;
        return await_work(sqe, iflags);
    }

    /** Receive a message from a socket asynchronously
     * @see recvmsg(2)
     * @see io_uring_enter(2) IORING_OP_RECVMSG
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */

    sqe_awaitable recvmsg(
        int sockfd,
        msghdr* msg,
        uint32_t flags,
        uint8_t iflags = 0
    ) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_recvmsg(sqe, sockfd, msg, flags);
        return await_work(sqe, iflags);
    }

    /** Send a message on a socket asynchronously
     * @see sendmsg(2)
     * @see io_uring_enter(2) IORING_OP_SENDMSG
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable sendmsg(
        int sockfd,
        const msghdr* msg,
        uint32_t flags,
        uint8_t iflags = 0
    ) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_sendmsg(sqe, sockfd, msg, flags);
        return await_work(sqe, iflags);
    }

    /** Receive a message from a socket asynchronously
     * @see recv(2)
     * @see io_uring_enter(2) IORING_OP_RECV
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable recv(
        int sockfd,
        void* buf,
        unsigned nbytes,
        uint32_t flags,
        uint8_t iflags = 0
    ) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_recv(sqe, sockfd, buf, nbytes, flags);
        return await_work(sqe, iflags);
    }

    /** Send a message on a socket asynchronously
     * @see send(2)
     * @see io_uring_enter(2) IORING_OP_SEND
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable send(
        int sockfd,
        const void* buf,
        unsigned nbytes,
        uint32_t flags,
        uint8_t iflags = 0
    ) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_send(sqe, sockfd, buf, nbytes, flags);
        return await_work(sqe, iflags);
    }

    /** Wait for an event on a file descriptor asynchronously
     * @see poll(2)
     * @see io_uring_enter(2)
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable poll(
        int fd,
        short poll_mask,
        uint8_t iflags = 0
    ) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_poll_add(sqe, fd, poll_mask);
        return await_work(sqe, iflags);
    }

    /** Enqueue a NOOP command, which eventually acts like pthread_yield when awaiting
     * @see io_uring_enter(2) IORING_OP_NOP
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable yield(
        uint8_t iflags = 0
    ) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_nop(sqe);
        return await_work(sqe, iflags);
    }

    /** Accept a connection on a socket asynchronously
     * @see accept4(2)
     * @see io_uring_enter(2) IORING_OP_ACCEPT
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable accept(
        int fd,
        sockaddr *addr,
        socklen_t *addrlen,
        int flags = 0,
        uint8_t iflags = 0
    ) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_accept(sqe, fd, addr, addrlen, flags);
        return await_work(sqe, iflags);
    }

    /** Initiate a connection on a socket asynchronously
     * @see connect(2)
     * @see io_uring_enter(2) IORING_OP_CONNECT
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable connect(
        int fd,
        sockaddr *addr,
        socklen_t addrlen,
        int flags = 0,
        uint8_t iflags = 0
    ) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_connect(sqe, fd, addr, addrlen);
        return await_work(sqe, iflags);
    }

    /** Wait for specified duration asynchronously
     * @see io_uring_enter(2) IORING_OP_TIMEOUT
     * @param ts initial expiration, timespec
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable timeout(
        __kernel_timespec *ts,
        uint8_t iflags = 0
    ) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_timeout(sqe, ts, 0, 0);
        return await_work(sqe, iflags);
    }

    /** Open and possibly create a file asynchronously
     * @see openat(2)
     * @see io_uring_enter(2) IORING_OP_OPENAT
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable openat(
        int dfd,
        const char *path,
        int flags,
        mode_t mode,
        uint8_t iflags = 0
    ) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_openat(sqe, dfd, path, flags, mode);
        return await_work(sqe, iflags);
    }

    /** Close a file descriptor asynchronously
     * @see close(2)
     * @see io_uring_enter(2) IORING_OP_CLOSE
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable close(
        int fd,
        uint8_t iflags = 0
    ) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_close(sqe, fd);
        return await_work(sqe, iflags);
    }

    /** Get file status asynchronously
     * @see statx(2)
     * @see io_uring_enter(2) IORING_OP_STATX
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable statx(
        int dfd,
        const char *path,
        int flags,
        unsigned mask,
        struct statx *statxbuf,
        uint8_t iflags = 0
    ) noexcept {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_statx(sqe, dfd, path, flags, mask, statxbuf);
        return await_work(sqe, iflags);
    }

    /** Splice data to/from a pipe asynchronously
     * @see splice(2)
     * @see io_uring_enter(2) IORING_OP_SPLICE
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable splice(
        int fd_in,
        loff_t off_in,
        int fd_out,
        loff_t off_out,
        size_t nbytes,
        unsigned flags,
        uint8_t iflags = 0
    ) {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_splice(sqe, fd_in, off_in, fd_out, off_out, nbytes, flags);
        return await_work(sqe, iflags);
    }

    /** Duplicate pipe content asynchronously
     * @see tee(2)
     * @see io_uring_enter(2) IORING_OP_TEE
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable tee(
        int fd_in,
        int fd_out,
        size_t nbytes,
        unsigned flags,
        uint8_t iflags = 0
    ) {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_tee(sqe, fd_in, fd_out, nbytes, flags);
        return await_work(sqe, iflags);
    }

    /** Shut down part of a full-duplex connection asynchronously
     * @see shutdown(2)
     * @see io_uring_enter(2) IORING_OP_SHUTDOWN
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable shutdown(
        int fd,
        int how,
        uint8_t iflags = 0
    ) {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_shutdown(sqe, fd, how);
        return await_work(sqe, iflags);
    }

    /** Change the name or location of a file asynchronously
     * @see renameat2(2)
     * @see io_uring_enter(2) IORING_OP_RENAMEAT
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable renameat(
        int olddfd,
        const char *oldpath,
        int newdfd,
        const char *newpath,
        unsigned flags,
        uint8_t iflags = 0
    ) {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_renameat(sqe, olddfd, oldpath, newdfd, newpath, flags);
        return await_work(sqe, iflags);
    }

    /** Create a directory asynchronously
     * @see mkdirat(2)
     * @see io_uring_enter(2) IORING_OP_MKDIRAT
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable mkdirat(
        int dirfd,
        const char *pathname,
        mode_t mode,
        uint8_t iflags = 0
    ) {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_mkdirat(sqe, dirfd, pathname, mode);
        return await_work(sqe, iflags);
    }

    /** Make a new name for a file asynchronously
     * @see symlinkat(2)
     * @see io_uring_enter(2) IORING_OP_SYMLINKAT
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable symlinkat(
        const char *target,
        int newdirfd,
        const char *linkpath,
        uint8_t iflags = 0
    ) {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_symlinkat(sqe, target, newdirfd, linkpath);
        return await_work(sqe, iflags);
    }

    /** Make a new name for a file asynchronously
     * @see linkat(2)
     * @see io_uring_enter(2) IORING_OP_LINKAT
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable linkat(
        int olddirfd,
        const char *oldpath,
        int newdirfd,
        const char *newpath,
        int flags,
        uint8_t iflags = 0
    ) {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_linkat(sqe, olddirfd, oldpath, newdirfd, newpath, flags);
        return await_work(sqe, iflags);
    }

    /** Delete a name and possibly the file it refers to asynchronously
     * @see unlinkat(2)
     * @see io_uring_enter(2) IORING_OP_UNLINKAT
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable unlinkat(
        int dfd,
        const char *path,
        unsigned flags,
        uint8_t iflags = 0
    ) {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_unlinkat(sqe, dfd, path, flags);
        return await_work(sqe, iflags);
    }

    /** Delete a name and possibly the file it refers to asynchronously
     * @see io_uring_enter(2) IORING_OP_MSG_RING
     * @param iflags IOSQE_* flags
     * @return a task object for awaiting
     */
    sqe_awaitable msg_ring(
        int fd,
        unsigned len,
        uint64_t data,
        unsigned flags,
        uint8_t iflags = 0
    ) {
        auto* sqe = io_uring_get_sqe_safe();
        io_uring_prep_msg_ring(sqe, fd, len, data, flags);
        return await_work(sqe, iflags);
    }

private:
    sqe_awaitable await_work(
        io_uring_sqe* sqe,
        uint8_t iflags
    ) noexcept {
        io_uring_sqe_set_flags(sqe, iflags);
        return sqe_awaitable(sqe);
    }

public:
    /** Get a sqe pointer that can never be NULL
     * @param ring pointer to inited io_uring struct
     * @return pointer to `io_uring_sqe` struct (not NULL)
     */
    [[nodiscard]]
    io_uring_sqe* io_uring_get_sqe_safe() noexcept {
        auto* sqe = io_uring_get_sqe(&ring);
        if (__builtin_expect(!!sqe, true)) {
            return sqe;
        } else {
            printf_if_verbose(__FILE__ ": SQ is full, flushing %u cqe(s)\n", cqe_count);
            io_uring_cq_advance(&ring, cqe_count);
            cqe_count = 0;
            io_uring_submit(&ring);
            sqe = io_uring_get_sqe(&ring);
            if (__builtin_expect(!!sqe, true)) return sqe;
            panic("io_uring_get_sqe", ENOMEM);
        }
    }

    /** Wait for an event forever, blocking
     * @see io_uring_wait_cqe
     * @see io_uring_enter(2)
     * @return a pair of promise pointer (used for resuming suspended coroutine) and retcode of finished command
     */
    template <typename T, bool nothrow>
    T run(const task<T, nothrow>& t) noexcept(nothrow) {
        while (!t.done()) {
            io_uring_submit_and_wait(&ring, 1);

            io_uring_cqe *cqe;
            unsigned head;

            io_uring_for_each_cqe(&ring, head, cqe) {
                ++cqe_count;
                auto coro = static_cast<resolver *>(io_uring_cqe_get_data(cqe));
                if (coro) coro->resolve(cqe->res);
            }

            printf_if_verbose(__FILE__ ": Found %u cqe(s), looping...\n", cqe_count);

            io_uring_cq_advance(&ring, cqe_count);
            cqe_count = 0;
        }

        return t.get_result();
    }

public:
    /** Register files for I/O
     * @param fds fds to register
     * @see io_uring_register(2) IORING_REGISTER_FILES
     */
    void register_files(std::initializer_list<int> fds) {
        register_files(fds.begin(), (unsigned int)fds.size());
    }
    void register_files(const int *files, unsigned int nr_files) {
        io_uring_register_files(&ring, files, nr_files) | panic_on_err("io_uring_register_files", false);
    }

    /** Update registered files
     * @see io_uring_register(2) IORING_REGISTER_FILES_UPDATE
     */
    void register_files_update(unsigned off, int *files, unsigned nr_files) {
        io_uring_register_files_update(&ring, off, files, nr_files) | panic_on_err("io_uring_register_files", false);
    }

    /** Unregister all files
     * @see io_uring_register(2) IORING_UNREGISTER_FILES
     */
    int unregister_files() noexcept {
        return io_uring_unregister_files(&ring);
    }

public:
    /** Register buffers for I/O
     * @param ioves array of iovec to register
     * @see io_uring_register(2) IORING_REGISTER_BUFFERS
     */
    template <unsigned int N>
    void register_buffers(iovec (&&ioves) [N]) {
        register_buffers(&ioves[0], N);
    }
    void register_buffers(const struct iovec *iovecs, unsigned nr_iovecs) {
        io_uring_register_buffers(&ring, iovecs, nr_iovecs) | panic_on_err("io_uring_register_buffers", false);
    }

    /** Unregister all buffers
     * @see io_uring_register(2) IORING_UNREGISTER_BUFFERS
     */
    int unregister_buffers() noexcept {
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
    unsigned cqe_count = 0;
    bool probe_ops[IORING_OP_LAST] = {};
};

} // namespace uio
