#include <fmt/color.h>
#include <fmt/core.h>

#include <liburing/io_service.hpp>
#include <string_view>
auto ping(uio::io_service& service, int read_fd, int write_fd) -> uio::task<> {
    std::string_view ping = "ping!";
    std::array<char, 64> buffer;
    for (int i = 0; i < 20; i++) {
        int count =
            co_await service.read(read_fd, buffer.data(), buffer.size(), 0);
        if (count < 0)
            uio::panic("Bad read", errno);

        auto recieved = std::string_view(buffer.data(), count);

        fmt::print(
            fg(fmt::color::cyan) | fmt::emphasis::bold,
            "ping: Recieved {}\n",
            recieved);
        if (recieved != "pong!")
            uio::panic("Unexpected message", 0);

        co_await service.write(write_fd, ping.data(), ping.size(), 0);
        co_await service.fsync(write_fd, 0);
    }
    close(read_fd);
    close(write_fd);
}
auto pong(uio::io_service& service, int read_fd, int write_fd) -> uio::task<> {
    std::string_view ping = "pong!";
    std::array<char, 64> buffer;
    for (int i = 0; i < 20; i++) {
        co_await service.write(write_fd, ping.data(), ping.size(), 0);
        co_await service.fsync(write_fd, 0);

        int count =
            co_await service.read(read_fd, buffer.data(), buffer.size(), 0);
        if (count < 0)
            uio::panic("Bad read", errno);

        auto recieved = std::string_view(buffer.data(), count);

        fmt::print(
            fg(fmt::color::magenta) | fmt::emphasis::bold,
            "pong: Recieved {}\n",
            recieved);
        if (recieved != "ping!")
            uio::panic("Unexpected message", 0);
    }
    close(read_fd);
    close(write_fd);
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
