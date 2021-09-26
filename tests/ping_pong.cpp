#include <fmt/color.h>
#include <fmt/core.h>

#include <liburing/io_service.hpp>
#include <string_view>

auto ping(uio::io_service& service, int read_fd, int write_fd) -> uio::task<> {
    std::string_view msg = "ping!";
    std::string_view expected = "pong!";
    std::array<char, 64> buffer;

    for (int i = 0; i < 20; i++) {
        int count =
            co_await service.read(read_fd, buffer.data(), buffer.size(), 0)
            | uio::panic_on_err("ping: Unable to read from read_fd", false);

        auto recieved = std::string_view(buffer.data(), count);

        fmt::print(
            fg(fmt::color::cyan) | fmt::emphasis::bold,
            "ping: Recieved {}\n",
            recieved);
        if (recieved != expected)
            uio::panic("Unexpected message", 0);

        co_await service.write(write_fd, msg.data(), msg.size(), 0)
            | uio::panic_on_err("ping: Unable to write to write_fd", false);
    }

    co_await service.close(write_fd)
        | uio::panic_on_err("ping: Unable to close write_fd", false);

    // Check for EOF before exiting
    if(0 != co_await service.read(read_fd, buffer.data(), buffer.size(), 0)) {
        throw std::runtime_error("pong: Pipe not at EOF like expected");
    }

    co_await service.close(read_fd)
        | uio::panic_on_err("ping: Unable to close read_fd", false);
}

auto pong(uio::io_service& service, int read_fd, int write_fd) -> uio::task<> {
    std::string_view msg = "pong!";
    std::string_view expected = "ping!";
    std::array<char, 64> buffer;

    for (int i = 0; i < 20; i++) {
        co_await service.write(write_fd, msg.data(), msg.size(), 0)
            | uio::panic_on_err("pong: Unable to write to write_fd", false);

        int count =
            co_await service.read(read_fd, buffer.data(), buffer.size(), 0)
            | uio::panic_on_err("pong: Unable to read from read_fd", false);

        auto recieved = std::string_view(buffer.data(), count);

        fmt::print(
            fg(fmt::color::magenta) | fmt::emphasis::bold,
            "pong: Recieved {}\n",
            recieved);
        if (recieved != expected)
            uio::panic("Unexpected message", 0);
    }

    co_await service.close(write_fd)
        | uio::panic_on_err("pong: Unable to close write_fd", false);

    // Check for EOF before exiting
    if(0 != co_await service.read(read_fd, buffer.data(), buffer.size(), 0)) {
        throw std::runtime_error("pong: Pipe not at EOF like expected");
    }

    co_await service.close(read_fd)
        | uio::panic_on_err("pong: Unable to close read_fd", false);
}

int main() {
    using uio::io_service;
    using uio::task;

    io_service io;

    std::array<int, 2> p1;
    std::array<int, 2> p2;
    pipe(p1.data()) | uio::panic_on_err("Unable to open pipe", true);
    pipe(p2.data()) | uio::panic_on_err("Unable to open pipe", true);

    // ping reads from p1 and writes to p2
    auto t1 = ping(io, p1[0], p2[1]);
    // pong writes to p1 and reads from p2
    auto t2 = pong(io, p2[0], p1[1]);

    io.run(t1);
    io.run(t2);
}
