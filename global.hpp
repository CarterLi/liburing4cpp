#pragma once

#include <unordered_map>
#include <string_view>
#include <vector>
#include <liburing.h>

enum {
    BUF_SIZE = 1 << 10,
};

using pool_ptr_t = std::vector<std::array<char, BUF_SIZE>>::pointer;

extern const std::unordered_map<std::string_view, std::string_view> MimeDicts;
extern io_uring ring;
extern std::vector<std::array<char, BUF_SIZE>> uring_buffers;
