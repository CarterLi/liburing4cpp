#pragma once
#include <string_view>
#include <array>
#include <utility>

#ifndef NDEBUG
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

    if (err == 0) err = errno;
    std::fprintf(stderr, "errno: %d\n", err);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wterminate"
    if (err == EPIPE) {
        throw std::runtime_error("Broken pipe: client socket is closed");
    }
    throw std::system_error(err, std::generic_category(), sv.data());
#pragma GCC diagnostic pop
}
