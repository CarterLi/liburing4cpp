#pragma once
#include <unordered_map>
#include <string_view>
#include <liburing.h>   // http://git.kernel.dk/liburing

extern const std::unordered_map<std::string_view, std::string_view> MimeDicts;

extern io_uring ring;

template <typename Fn>
struct on_scope_exit {
    on_scope_exit(Fn &&fn): _fn(std::move(fn)) {}
    ~on_scope_exit() { this->_fn(); }

private:
    Fn _fn;
};
