#include <liburing/io_service.hpp>
#include <string_view>

#include <fmt/core.h>

auto write_to_fd(uio::io_service& service, int fd) -> uio::task<> {
    std::string_view message = "Hello, world!";
    fmt::print("Sending data...\n");
    co_await service.write(fd, message.data(), message.size(), 0)
        | uio::panic_on_err("Unable to write to file descriptor", true);

    close(fd);
    fmt::print("Wrote message to {}\n", fd);
    fmt::print("write_to_fd completed.\n");
}
auto read_from_fd(uio::io_service& service, int fd) -> uio::task<> {
    std::array<char, 1024> buffer;

    fmt::print("Waiting for data...\n");

    int result = 0;
    while (true) {
        result = co_await service.read(fd, buffer.data(), buffer.size(), 0);

        if (result <= 0)
            break;
        std::string_view data(buffer.data(), result);
        fmt::print("Recieved message '{}' from {}\n", data, fd);
    }
    if (result == 0) {
        fmt::print("EOF reached. read_from_fd completed.\n", result);
    } else {
        uio::panic("Recieved bad result", errno);
    }
}
int main() {
    using uio::io_service;
    using uio::task;

    io_service io;

    std::array<int, 2> fds;
    pipe(fds.data()) | uio::panic_on_err("Unable to open pipe", true);

    auto t1 = read_from_fd(io, fds[0]);
    auto t2 = write_to_fd(io, fds[1]);

    io.run(t1);
    io.run(t2);
}
