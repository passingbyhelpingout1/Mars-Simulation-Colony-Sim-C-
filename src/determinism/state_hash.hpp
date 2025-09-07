#pragma once
#include <cstdint>
#include <cstddef>

namespace sim::det {

inline uint64_t fnv1a64(uint64_t h, const void* data, size_t n) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

inline uint64_t u32_le(uint64_t h, uint32_t v) {
    unsigned char b[4] = {
        static_cast<unsigned char>(v & 0xFFu),
        static_cast<unsigned char>((v >> 8) & 0xFFu),
        static_cast<unsigned char>((v >> 16) & 0xFFu),
        static_cast<unsigned char>((v >> 24) & 0xFFu)
    };
    return fnv1a64(h, b, 4);
}

inline uint64_t u64_le(uint64_t h, uint64_t v) {
    unsigned char b[8] = {
        static_cast<unsigned char>(v & 0xFFu),
        static_cast<unsigned char>((v >> 8) & 0xFFu),
        static_cast<unsigned char>((v >> 16) & 0xFFu),
        static_cast<unsigned char>((v >> 24) & 0xFFu),
        static_cast<unsigned char>((v >> 32) & 0xFFu),
        static_cast<unsigned char>((v >> 40) & 0xFFu),
        static_cast<unsigned char>((v >> 48) & 0xFFu),
        static_cast<unsigned char>((v >> 56) & 0xFFu)
    };
    return fnv1a64(h, b, 8);
}

} // namespace sim::det
