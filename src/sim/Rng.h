#pragma once
#include <cstdint>

namespace sim {

// SplitMix64 to expand a 64-bit seed into well-distributed 64-bit values.
static inline uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

// Tiny, fast, deterministic PCG32 PRNG.
class Rng {
public:
    explicit Rng(uint64_t seed = 0xC0FFEEULL) { seed_pcg(seed); }

    void seed_pcg(uint64_t seed) {
        // Derive state/inc from the single seed; inc must be odd.
        state_ = splitmix64(seed);
        inc_   = (splitmix64(seed ^ 0xDA442D24ULL) << 1u) | 1u;
        // Advance away from zero-state corner case.
        next_u32();
    }

    // 32-bit output (authoritative)
    uint32_t next_u32() {
        uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
        uint32_t rot = static_cast<uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-static_cast<int32_t>(rot)) & 31));
    }

    uint64_t next_u64() {
        return (static_cast<uint64_t>(next_u32()) << 32) | next_u32();
    }

    // Unbiased [0, n) using Lemire’s method (with rejection fix-up)
    uint32_t uniform_u32(uint32_t n) {
        if (n == 0) return 0;
        uint64_t m = static_cast<uint64_t>(next_u32()) * n;
        uint32_t lo = static_cast<uint32_t>(m);
        if (lo < n) {
            uint32_t t = static_cast<uint32_t>(-n) % n;
            while (lo < t) {
                m = static_cast<uint64_t>(next_u32()) * n;
                lo = static_cast<uint32_t>(m);
            }
        }
        return static_cast<uint32_t>(m >> 32);
    }

    // Inclusive range [lo, hi]
    int uniform_int(int lo, int hi) {
        const uint32_t span = static_cast<uint32_t>(hi - lo + 1);
        return lo + static_cast<int>(uniform_u32(span));
    }

    // [0,1) double with 53 bits of precision
    double uniform01() {
        return (next_u64() >> 11) * (1.0 / 9007199254740992.0);
    }

private:
    uint64_t state_ = 0;
    uint64_t inc_   = 0xDA442D24E4A4ULL | 1u;
};

} // namespace sim
