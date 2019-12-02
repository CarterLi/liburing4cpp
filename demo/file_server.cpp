#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <chrono>
#include <cerrno>
#include <fmt/format.h> // https://github.com/fmtlib/fmt

#include "io_service.hpp"
#include "mime_dicts.hpp"
#include "when.hpp"

enum {
    SERVER_PORT = 8080,
    BUF_SIZE = 1024,
};

using namespace std::literals;

// Predefined HTTP error response headers
static constexpr const auto http_404_hdr = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"sv;
static constexpr const auto http_400_hdr = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"sv;

int runningCoroutines = 0;

// Serve response
task<> http_send_file(io_service& service, std::string filename, int clientfd, int dirfd) {
    if (filename == "./") filename = "./index.html";

    const auto infd = openat(dirfd, filename.c_str(), O_RDONLY);
    on_scope_exit closefd([=]() { close(infd); });

    if (struct stat st; infd < 0 || fstat(infd, &st) || !S_ISREG(st.st_mode)) {
        fmt::print("{}: file not found!\n", filename);
        co_await service.sendmsg(clientfd, { to_iov(http_404_hdr) }, MSG_NOSIGNAL);
    } else {
        auto contentType = [filename_view = std::string_view(filename)]() {
            auto extension = filename_view.substr(filename_view.find_last_of('.') + 1);
            auto iter = MimeDicts.find(extension);
            if (iter == MimeDicts.end()) return "application/octet-stream"sv;
            return iter->second;
        }();

        co_await service.sendmsg(clientfd, {
            to_iov(fmt::format("HTTP/1.1 200 OK\r\nContent-type: {}\r\nContent-Length: {}\r\n\r\n", contentType, st.st_size)),
        }, MSG_NOSIGNAL | MSG_MORE);

        off_t offset = 0;
        std::array<char, BUF_SIZE> filebuf;
        auto iov = to_iov(filebuf);
        for (; st.st_size - offset > BUF_SIZE; offset += BUF_SIZE) {
            co_await when_all(std::array {
                service.readv(infd, { iov }, offset, IOSQE_IO_LINK),
                service.sendmsg(clientfd, { iov }, MSG_NOSIGNAL | MSG_MORE),
            });
            co_await service.delay(1s); // For debugging
        }
        if (st.st_size > offset) {
            iov.iov_len = size_t(st.st_size - offset);
            co_await when_all(std::array {
                service.readv(infd, { iov }, offset, IOSQE_IO_LINK),
                service.sendmsg(clientfd, { iov }, MSG_NOSIGNAL),
            });
        }
    }
}

// Parse HTTP request header
task<> serve(io_service& service, int clientfd, int dirfd) {
    fmt::print("Serving connection, sockfd {}; number of running coroutines: {}\n",
         clientfd, runningCoroutines);

    std::array<char, BUF_SIZE> buffer;
    int res = co_await service.recvmsg(clientfd, { to_iov(buffer) }, MSG_NOSIGNAL);

    std::string_view buf_view = std::string_view(buffer.data(), size_t(res));

    // We only handle GET requests, for simplification
    if (buf_view.compare(0, 3, "GET") == 0) {
        auto file = "."s += buf_view.substr(4, buf_view.find(' ', 4) - 4);
        fmt::print("received request {} with sockfd {}\n", file, clientfd);
        co_await http_send_file(service, file, clientfd, dirfd);
    } else {
        fmt::print("unsupported request: {}\n", buf_view);
        co_await service.sendmsg(clientfd, { to_iov(http_400_hdr) }, MSG_NOSIGNAL);
    }
}

task<> accept_connection(io_service& service, int serverfd, int dirfd) {
    while (int clientfd = co_await service.accept(serverfd, nullptr, nullptr)) {
        // Start worker coroutine to handle new requests
        [=, &service](int clientfd) -> task<> {
            ++runningCoroutines;
            auto start = std::chrono::high_resolution_clock::now();
            try {
                co_await serve(service, clientfd, dirfd);
            } catch (std::exception& e) {
                fmt::print("sockfd {} crashed with exception: {}\n",
                    clientfd,
                    e.what());
            }

            // Clean up
            close(clientfd);
            fmt::print("sockfd {} is closed, time used {}\n",
                clientfd,
                (std::chrono::high_resolution_clock::now() - start).count());
            --runningCoroutines;
        }(clientfd);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fmt::print("Usage: {} <ROOT_DIR>\n", argv[0]);
        return 1;
    }

    int dirfd = open(argv[1], O_DIRECTORY);
    if (dirfd < 0) panic("open dir");
    on_scope_exit closedir([=]() { close(dirfd); });

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) panic("socket creation");
    on_scope_exit closesock([=]() { close(sockfd); });

    if (int on = 1; setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) panic("SO_REUSEADDR");
    if (int on = 1; setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on))) panic("SO_REUSEPORT");

    if (sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(SERVER_PORT),
        .sin_addr = { INADDR_ANY },
        .sin_zero = {}, // Silense compiler warnings
    }; bind(sockfd, reinterpret_cast<sockaddr *>(&addr), sizeof (sockaddr_in))) panic("socket binding");

    if (listen(sockfd, 128)) panic("listen");
    fmt::print("Listening: {}\n", SERVER_PORT);

    io_service service;

    // Start main coroutine ( for co_await )
    auto work = accept_connection(service, sockfd, dirfd);

    // Event loop
    while (!work.done()) {
        if (auto some = service.timedwait_event(1s)) {
            auto [promise, res] = some.value();

            // Found a finished event, go back to its coroutine.
            promise->resolve(res);
        } else {
            fmt::print("{}: PING!\n", std::chrono::system_clock::now().time_since_epoch().count() / 1000'000);
        }
    }

    work.get_result();
}
