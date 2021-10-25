#pragma once
#include <string_view>
#include <array>
#include <utility>
#include <system_error>

#if !defined(NDEBUG) && __has_include(<execinfo.h>)
#   include <execinfo.h>
#endif

template <typename Fn>
struct on_scope_exit {
    [[nodiscard]]
    on_scope_exit(Fn &&fn) noexcept: _fn(std::move(fn)) {}
    ~on_scope_exit() { this->_fn(); }

private:
    Fn _fn;
};

[[noreturn]]
void panic(std::string_view sv, int err = 0) noexcept {
    if (err == 0) {
#ifdef _WIN32
        err = GetLastError();
#else
        err = errno;
#endif
    }
#ifdef _WIN32
    std::fprintf(stderr, "LastError: %x\n", (unsigned)err);
#else
    std::fprintf(stderr, "errno: %d\n", err);
#endif

#if !defined(NDEBUG) && __has_include(<execinfo.h>)
    // https://stackoverflow.com/questions/77005/how-to-automatically-generate-a-stacktrace-when-my-program-crashes
    void *array[32];
    size_t size;

    // get void*'s for all entries on the stack
    size = backtrace(array, 32);

    // print out all the frames to stderr
    backtrace_symbols_fd(array, size, STDERR_FILENO);

    // __asm__("int $3");
#endif

    std::fprintf(stderr, "Error: %s\n", std::system_error(err, std::system_category(), sv.data()).what());
    std::terminate();
}
