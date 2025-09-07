#pragma once
#include <chrono>
#include "mars/timing.hpp"

namespace eng {
  using clock       = std::chrono::steady_clock;
  using seconds_f64 = std::chrono::duration<double>;
  // DO NOT define kFixedDt here; use mars::kFixedDt from mars/timing.hpp
}
