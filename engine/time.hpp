#pragma once
#include <chrono>

namespace eng {
  using clock        = std::chrono::steady_clock;              // monotonic
  using seconds_f64  = std::chrono::duration<double>;
  constexpr double   kFixedDt = 1.0 / 20.0;                    // 20 Hz simulation
}
