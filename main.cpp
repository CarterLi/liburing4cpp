#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <chrono>
#include <liburing.h>   // http://git.kernel.dk/liburing
#include <fmt/format.h> // https://github.com/fmtlib/fmt

#include "coroutine.hpp"

enum {
    SERVER_PORT = 8080,
    BUF_NUMBER = 4, // 经测至多12个，多了报 EFAULT
};

using namespace std::literals;

// 一些预定义的错误返回体
static constexpr auto http_404_hdr = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n"sv;
static constexpr auto http_400_hdr = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n"sv;

// 解析到HTTP请求的文件后，发送本地文件系统中的文件
void http_send_file(Coroutine& fiber, std::string filename, int clientfd, int dirfd, pool_ptr_t ppool) {
    if (filename == "./") filename = "./index.html";

    // 尝试打开待发送文件
    const auto infd = openat(dirfd, filename.c_str(), O_RDONLY);
    on_scope_exit closefd([=]() { close(infd); });

    if (struct stat st; infd < 0 || fstat(infd, &st) || !S_ISREG(st.st_mode)) {
        // 文件未找到情况下发送404 error响应
        fmt::print("{}: file not found!\n", filename);
        fiber.await_writev(clientfd, { to_iov(http_404_hdr) });
    } else {
        auto contentType = [filename_view = std::string_view(filename)]() {
            auto extension = filename_view.substr(filename_view.find_last_of('.') + 1);
            auto iter = MimeDicts.find(extension);
            if (iter == MimeDicts.end()) return "application/octet-stream"sv;
            return iter->second;
        }();

        // 发送响应头
        fiber.await_writev(clientfd, {
            to_iov(fmt::format("HTTP/1.1 200 OK\r\nContent-type: {}\r\nContent-Length: {}\r\n\r\n", contentType, st.st_size)),
        });

        off_t offset = 0;
        if (ppool) {
            // 一次读取 BUF_SIZE 个字节数据并发送
            for (; st.st_size - offset > BUF_SIZE; offset += BUF_SIZE) {
                fiber.await_read_fixed(ppool, infd, BUF_SIZE, offset);
                fiber.await_write_fixed(ppool, clientfd, BUF_SIZE);
                fiber.delay(1); // For debugging
            }
            // 读取剩余数据并发送
            if (st.st_size > offset) {
                fiber.await_read_fixed(ppool, infd, size_t(st.st_size - offset), offset);
                fiber.await_write_fixed(ppool, clientfd, size_t(st.st_size - offset));
            }
        } else {
            std::array<char, BUF_SIZE> filebuf;
            auto iov = to_iov(filebuf);
            for (; st.st_size - offset > BUF_SIZE; offset += BUF_SIZE) {
                fiber.await_readv(infd, { iov }, offset);
                fiber.await_writev(clientfd, { iov });
                fiber.delay(1); // For debugging
            }
            if (st.st_size > offset) {
                iov.iov_len = size_t(st.st_size - offset);
                fiber.await_readv(infd, { iov }, offset);
                fiber.await_writev(clientfd, { iov });
            }
        }
    }
}

// HTTP请求解析
void serve(Coroutine& fiber, int clientfd, int dirfd, pool_ptr_t ppool) {
    fmt::print("Serving connection, sockfd {} with pool: {}; number of running coroutines: {}\n",
         clientfd, static_cast<void *>(ppool), Coroutine::runningCoroutines);

    std::string_view buf_view;
    std::array<char, BUF_SIZE> buffer;
    // 优先使用缓存池
    if (ppool) {
        // 缓冲区可用时直接加载数据至缓冲区内
        int res = fiber.await_read_fixed(ppool, clientfd);
        buf_view = std::string_view(ppool->data(), size_t(res));
    } else {
        // 不可用则另找内存加载数据
        int res = fiber.await_readv(clientfd, { to_iov(buffer) });
        buf_view = std::string_view(buffer.data(), size_t(res));
    }

    // 这里我们只处理GET请求
    if (buf_view.compare(0, 3, "GET") == 0) {
        // 获取请求的path
        auto file = "."s += buf_view.substr(4, buf_view.find(' ', 4) - 4);
        fmt::print("received request {} with sockfd {}\n", file, clientfd);
        http_send_file(fiber, file, clientfd, dirfd, ppool);
    } else {
        // 其他HTTP请求处理，如POST，HEAD等，返回400错误
        fmt::print("unsupported request: {}\n", buf_view);
        fiber.await_writev(clientfd, { to_iov(http_400_hdr) });
    }
}

// 开辟缓冲区池，用以减少与内核通信所需内存映射次数
std::vector<pool_ptr_t> register_buffers() {
    // 用一个集合保存可用的缓冲区
    std::vector<pool_ptr_t> result(uring_buffers.size());
    if (!result.empty()) {
        std::vector<iovec> iov_pool(uring_buffers.size());
        for (size_t i = 0; i < uring_buffers.size(); ++i) {
            iov_pool[i] = to_iov(uring_buffers[i]);
            result[i] = &uring_buffers[i];
        }
        // 注册缓冲区
        if (io_uring_register(ring.ring_fd, IORING_REGISTER_BUFFERS, iov_pool.data(), unsigned(iov_pool.size()))) {
            fmt::print("registering io_uring buffers failed. "
                       "will always allocate memory on each I/O submission, "
                       "which can affect performance.\n");
            result.clear();
        }
    }
    return result;
}

void accept_connection(Coroutine& fiber, int serverfd, int dirfd) {
    auto available_buffers = register_buffers();

    while (fiber.await_poll(serverfd)) {
        int clientfd = accept(serverfd, nullptr, nullptr);
        // 注册必要数据，这个数据对整个协程都可用
        pool_ptr_t ppool = nullptr;
        if (!available_buffers.empty()) {
            // 如果池中有可用的缓冲区，使用之
            ppool = available_buffers.back();
            available_buffers.pop_back();
        }
        // 新建新协程处理请求
        new Coroutine(
            [=](Coroutine::BaseType& fiber) {
                serve(static_cast<Coroutine &>(fiber), clientfd, dirfd, ppool);
            },
            [=, &available_buffers, start = std::chrono::high_resolution_clock::now()] () {
                // 请求结束时清理资源
                close(clientfd);
                fmt::print("sockfd {} is closed, time used {}, with pool {}\n",
                    clientfd,
                    (std::chrono::high_resolution_clock::now() - start).count(),
                    static_cast<void *>(ppool));
                if (ppool) {
                    // 将缓冲区重新放入可用列表
                    available_buffers.push_back(ppool);
                }
            }
        );
    }
}

int main(int argc, char* argv[]) {
    if (argc > 3 || argc < 2 || (argc == 3 && std::string_view(argv[2]) != "--use-buffer")) {
        fmt::print("Usage: {} <ROOT_DIR> [--use-buffer]\n", argv[0]);
        return 1;
    }

    if (argc == 3) {
        uring_buffers.resize(BUF_NUMBER);
    }

    int dirfd = open(argv[1], O_DIRECTORY);
    if (dirfd < 0) panic("open dir");
    on_scope_exit closedir([=]() { close(dirfd); });

    // 初始化IO循环队列，内核支持的原生异步操作实现
    if (io_uring_queue_init(32, &ring, 0)) panic("queue_init");
    on_scope_exit closerg([&]() { io_uring_queue_exit(&ring); });


    // 建立TCP套接字
    int sockfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sockfd < 0) panic("socket creation");
    on_scope_exit closesock([=]() { close(sockfd); });

    // 设置允许端口重用
    int on = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on))) panic("SO_REUSEADDR");
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof(on))) panic("SO_REUSEPORT");

    sockaddr_in addr = {
        AF_INET,
        // 这里要注意，端口号一定要使用htons先转化为网络字节序，否则绑定的实际端口可能和你需要的不同
        htons(SERVER_PORT),
        { INADDR_ANY },
        {}, // 消除编译器警告
    };
    // 绑定端口
    if (bind(sockfd, reinterpret_cast<sockaddr *>(&addr), sizeof (sockaddr_in))) panic("socket binding");
    // 监听端口
    if (listen(sockfd, 128)) panic("listen");
    fmt::print("Listening: {}\n", SERVER_PORT);

    new Coroutine(
        [=](Coroutine::BaseType& fiber) { accept_connection(static_cast<Coroutine &>(fiber), sockfd, dirfd); }
    );

    // 事件循环
    while (Coroutine::runningCoroutines) {
        // 获取已完成的IO事件
        io_uring_cqe* cqe;
        if (io_uring_wait_cqe(&ring, &cqe)) panic("wait_cqe");

        // 有已完成的事件，回到协程继续
        auto* fiber = static_cast<Coroutine *>(io_uring_cqe_get_data(cqe));
        io_uring_cqe_seen(&ring, cqe);
        if (!fiber->next(cqe->res)) delete fiber;
    }
}
