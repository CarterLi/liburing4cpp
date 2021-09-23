#pragma once
#include <stdio.h>
#include "io_host.hpp"
#if USE_FIXED_FILE
#   undef USE_FIXED_FILE
#   define USE_FIXED_FILE IOSQE_FIXED_FILE
#   include <immintrin.h>
#   include <vector>

std::vector<__m256i> vec;

void init_fixed_files(io_host& host, size_t client_num) noexcept {
    auto size = client_num / 2 + 1;
    vec.resize(size, _mm256_set1_epi32(-1));
    host.register_files((int *)vec.data(), size * sizeof (__m256i) / sizeof (int32_t));
}

int32_t unregister_file(io_host& host, int32_t idx) noexcept {
    host.update_file(idx);
    return std::exchange(((int *)vec.data())[idx], -1);
}

int32_t get_file(int32_t idx) noexcept {
    return ((int *)vec.data())[idx];
}

int32_t register_file(io_host& host, int32_t fd) noexcept {
    auto pack = _mm256_set1_epi32(-1);
    uint32_t i = 0, offset = 0;
    for (auto ymm : vec) {
        uint32_t mask = _mm256_movemask_epi8(_mm256_cmpeq_epi32(ymm, pack));
        if (mask) {
            offset = _tzcnt_u32(mask) / sizeof (int32_t);
            break;
        }
        ++i;
    }
    if (__builtin_expect(i >= vec.size(), false)) panic("register_file", ENOMEM);

    int32_t result = i * sizeof (__m256i) / sizeof (int32_t) + offset;
    host.update_file(result, fd);
    ((int *)vec.data())[result] = fd;
    return result;
}
#else
inline void init_fixed_files(io_host&, size_t) noexcept {}
inline int32_t unregister_file(io_host&, int32_t fd) noexcept {
    return fd;
}
inline int32_t get_file(int32_t idx) noexcept {
    return idx;
}
inline int32_t register_file(io_host&, int32_t fd) noexcept {
    return fd;
}
#endif
