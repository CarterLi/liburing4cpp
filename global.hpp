#pragma once
#include <unordered_map>
#include <string_view>
#include <vector>
#include <liburing.h>   // http://git.kernel.dk/liburing

extern const std::unordered_map<std::string_view, std::string_view> MimeDicts;

extern io_uring ring;
