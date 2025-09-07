#pragma once
#include <cstdint>
#include <vector>
#include <random>
#include "types.hpp"

struct GameState {
    // time
    Hours hour{0};

    // deterministic RNG
    uint32_t rngSeed{123456u};
    std::mt19937 rng{rngSeed};

    // resources
    double power_kw{0.0};
    double water{100.0};
    double oxygen{100.0};
    double food{100.0};
    int    credits{2000};
    int    metals{50};

    // buildings inventory (counts per type)
    std::array<int, to_index(BuildingType::COUNT)> count{};

    void setSeed(uint32_t seed) { rngSeed = seed; rng.seed(seed); }
};
