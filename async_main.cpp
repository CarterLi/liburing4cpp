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

#include "global.hpp"
#include "async_coro.hpp"

enum {
    SERVER_PORT = 8080,
    BUF_SIZE = 1024,
};

using namespace std::literals;

// 一些预定义的错误返回体
static constexpr const auto http_404_hdr = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"sv;
static constexpr const auto http_400_hdr = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"sv;

task<bool> test() {
    std::vector<task<int>> vec;
    vec.emplace_back(async_delay(1));
    vec.emplace_back(async_delay(2));
    vec.emplace_back(async_delay(3));
    co_await taskAll<int>(std::move(vec));
    co_return true;
}

int main(int argc, char* argv[]) {
    // 初始化内核支持的原生异步IO操作实现
#if !USE_LIBAIO
    if (io_uring_queue_init(32, &ring, 0)) panic("queue_init");
    on_scope_exit closerg([&]() { io_uring_queue_exit(&ring); });
#else
    if (io_queue_init(32, &context)) panic("queue_init");
    on_scope_exit closerg([&]() { io_queue_release(context); });
#endif

    auto res = test();
    res.start();

    // 事件循环
    while (!res.done()) {
        // 获取已完成的IO事件
        auto [coro, res] = wait_for_event();

        // 有已完成的事件，回到协程继续
        try {
            coro->resolve(res);
        } catch (std::runtime_error& e) {
            fmt::print("{}\n", e.what());
            delete coro;
        }

        fmt::print("OK\n");
    }

}
