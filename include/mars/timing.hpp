#pragma once
#include <cstdint>

namespace mars {

// Pick ONE rate. 20 Hz is plenty for a colony sim; set to 50 if you prefer.
inline constexpr int kSimHz = 20;

inline constexpr int64_t kTickMicros = 1'000'000 / kSimHz;           // for clocks
inline constexpr double  kFixedDt    = 1.0 / static_cast<double>(kSimHz); // for integrators

} // namespace mars
