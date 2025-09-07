#pragma once
#include <random>
#include <cstdint>

struct RNG {
    explicit RNG(uint64_t seed) : eng(seed) {}
    // Dependency-inject this into systems instead of using globals
    uint64_t u64() { return dist64(eng); }
    int     uniformInt(int a, int b) { return std::uniform_int_distribution<int>(a,b)(eng); }
    double  uniform01() { return std::uniform_real_distribution<double>(0.0,1.0)(eng); }

private:
    std::mt19937_64 eng;
    std::uniform_int_distribution<uint64_t> dist64;
};
