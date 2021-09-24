#pragma once
#include "io_host.hpp"
#if USE_FIXED_FILE
#   undef USE_FIXED_FILE
#   define USE_FIXED_FILE IOSQE_FIXED_FILE
#   include <immintrin.h>
#   include <vector>

struct fixed_file_handler {
    void init(io_host& host) noexcept {
        phost = &host;
        auto size = host.ring_params.sq_entries / 2 + 1;
        vec.resize(size, _mm256_set1_epi32(-1));
        host.register_files((int *)vec.data(), size * sizeof (__m256i) / sizeof (int32_t));
    }

    int32_t register_file(int32_t fd) noexcept {
        auto idx = find_empty_slot();
        (*this)[idx] = fd;
        phost->update_file(idx, fd);
        return idx;
    }

    int32_t unregister_file(int32_t idx) noexcept {
        phost->update_file(idx);
        return std::exchange((*this)[idx], -1);
    }

    int32_t& operator [](int32_t idx) noexcept {
        return ((int *)vec.data())[idx];
    }

    int32_t find_empty_slot() const noexcept {
        auto pack = _mm256_set1_epi32(-1);
        int32_t i = 0, offset = 0;
        for (auto ymm : vec) {
            uint32_t mask = _mm256_movemask_epi8(_mm256_cmpeq_epi32(ymm, pack));
            if (mask) {
                offset = _tzcnt_u32(mask) / sizeof (int32_t);
                break;
            }
            ++i;
        }
        if (__builtin_expect(i >= vec.size(), false)) panic("find_empty_slot", ENOMEM);
        return i * sizeof (__m256i) / sizeof (int32_t) + offset;
    }

private:
    io_host* phost = nullptr;
    std::vector<__m256i> vec;
};
#else
struct fixed_file_handler {
    void init(io_host&) noexcept {}
    int32_t register_file(int32_t fd) const noexcept {
        return fd;
    }
    int32_t unregister_file(int32_t fd) const noexcept {
        return fd;
    }
    int32_t operator [](int32_t idx) const noexcept {
        return idx;
    }
};
#endif
