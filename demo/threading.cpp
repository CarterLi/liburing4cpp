#include <sys/eventfd.h>
#include <memory>
#include <thread>
#include <type_traits>
#include <variant>

#include <fmt/format.h>

#include "io_service.hpp"

template <typename Fn>
task<std::invoke_result_t<Fn>> invoke(io_service& service, Fn&& fn) {
    static_assert (noexcept(fn()));
    static_assert (sizeof (eventfd_t) >= sizeof (intptr_t));
    using result_t = std::invoke_result_t<Fn>;
    int efd = ::eventfd(0, O_CLOEXEC);
    on_scope_exit closefd([=]() { ::close(efd); });
    std::thread([](Fn&& fn, int efd) {
        if constexpr (std::is_void_v<result_t>) {
            fn();
            ::eventfd_write(efd, 1);
        } else {
            ::eventfd_write(efd, (eventfd_t) (intptr_t (new result_t(fn()))));
        }
    }, std::move(fn), efd).detach();

    eventfd_t value = -1;
    co_await service.read(efd, &value, sizeof (value), 0);
    if constexpr (!std::is_void_v<result_t>) {
        co_return *std::unique_ptr<result_t>((result_t *) (intptr_t (value)));
    }
}

template <typename Fn>
task<std::invoke_result_t<Fn>> invoke2(io_service& service, Fn&& fn) noexcept(noexcept(fn())) {
    using result_t = std::invoke_result_t<Fn>;
    int efd = ::eventfd(0, O_CLOEXEC);
    on_scope_exit closefd([=]() { ::close(efd); });
    std::variant<
        std::monostate,
        std::conditional_t<std::is_void_v<result_t>, std::monostate, result_t>,
        std::conditional_t<noexcept (fn()), std::monostate, std::exception_ptr>
    > result;
    std::thread([&]() {
        on_scope_exit writefd([=]() { ::eventfd_write(efd, 1); });
        try {
            if constexpr (std::is_void_v<result_t>) {
                fn();
            } else {
                std::atomic_signal_fence(std::memory_order_acquire);
                result.template emplace<1>(fn());
                std::atomic_signal_fence(std::memory_order_release);
            }
        } catch (...) {
            result.template emplace<2>(std::current_exception());
        }
    }).detach();
    co_await service.poll(efd, POLLIN);

    if constexpr (!noexcept (fn())) {
        if (result.index() == 2) std::rethrow_exception(std::get<2>(result));
    }

    if constexpr (!std::is_void_v<result_t>) {
        co_return std::move(std::get<1>(result));
    }
}

int main() {
    io_service service;
    using namespace std::chrono_literals;

    auto work = [&] () -> task<> {
        auto res = co_await invoke2(service, []() noexcept -> std::string {
            std::this_thread::sleep_for(2s);
            return "OK";
        });
        fmt::print("{}\n", res);
    }();

    while (!work.done()) {
        auto [promise, res] = service.wait_event();
        promise->resolve(res);
    }
}
