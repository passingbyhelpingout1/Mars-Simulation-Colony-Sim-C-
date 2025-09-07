#pragma once
#include <random>
#include <cstdint>

// Single authoritative RNG wrapper. Deterministic across platforms with same seed.
class Rng {
public:
    explicit Rng(uint64_t seed = 0xC0FFEEULL) : engine_(static_cast<std::mt19937::result_type>(seed)) {}

    uint32_t u32() { return engine_(); }

    // [0,1)
    double uniform01() {
        // Convert to double in a reproducible way without distributions
        return (engine_() / static_cast<double>(std::mt19937::max() + 1.0));
    }

    int range_int(int lo, int hi_inclusive) {
        std::uniform_int_distribution<int> dist(lo, hi_inclusive);
        return dist(engine_);
    }

private:
    std::mt19937 engine_;
};
