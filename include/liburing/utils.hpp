#pragma once
#include <unistd.h>
#include <fcntl.h>
#include <string_view>
#include <time.h>

namespace uio {
/** Fill an iovec struct using buf & size */
constexpr inline iovec to_iov(void *buf, size_t size) noexcept {
    return { buf, size };
}
/** Fill an iovec struct using string view */
constexpr inline iovec to_iov(std::string_view sv) noexcept {
    return to_iov(const_cast<char *>(sv.data()), sv.size());
}
/** Fill an iovec struct using std::array */
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

    // __asm__("int $3");
#endif

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
template <bool nothrow>
inline task<int> operator |(task<int, nothrow> tret, panic_on_err&& poe) {
    co_return (co_await tret) | std::move(poe);
}
inline task<int> operator |(sqe_awaitable tret, panic_on_err&& poe) {
    co_return (co_await tret) | std::move(poe);
}

} // namespace uio
