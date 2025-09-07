#pragma once
#include <cstdint>
#include <random>
#include <array>

// Lightweight deterministic RNG wrapper.
// NOTE: std::mt19937_64 is deterministic given the same state; we avoid random_device.
// If you need cross-compiler identical replays, switch to a tiny PCG32/xoshiro impl later.
struct Rng {
  std::mt19937_64 gen;

  explicit Rng(uint64_t seed) : gen(seed) {}

  // Helpers so you don't sprinkle distributions everywhere.
  uint64_t next_u64() { return gen(); }

  // Inclusive range [lo, hi].
  int next_int(int lo, int hi) {
    std::uniform_int_distribution<int> d(lo, hi);
    return d(gen);
  }

  // [0,1)
  double next_unit() {
    std::uniform_real_distribution<double> d(0.0, std::nextafter(1.0, std::numeric_limits<double>::max()));
    return d(gen);
  }
};
