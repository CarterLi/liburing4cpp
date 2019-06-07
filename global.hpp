#pragma once
#include <unordered_map>
#include <string_view>
#include <vector>
#if !USE_LIBAIO
#   include <liburing.h>   // http://git.kernel.dk/liburing
#else
#   include <libaio.h>     // http://git.infradead.org/users/hch/libaio.git
#endif

extern const std::unordered_map<std::string_view, std::string_view> MimeDicts;

#if !USE_LIBAIO
extern io_uring ring;
#else
extern io_context_t context;
#endif
