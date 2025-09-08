#pragma once
#include <cstdint>
#include "mars/timing.hpp"

class SimClock {
public:
  // Tick size comes from a single source of truth.
  static constexpr int64_t kTickMicros = mars::kTickMicros;

  // Advance the accumulator by the elapsed frame time (in microseconds),
  // clamped to a sane range to avoid spiraling after long stalls.
  void advance_by_frame_us(int64_t frame_dt_us) noexcept {
    if (frame_dt_us < 0)       frame_dt_us = 0;
    if (frame_dt_us > 250'000) frame_dt_us = 250'000; // 250 ms cap
    accumulator_us_ += frame_dt_us;
  }

  bool   step_ready() const noexcept { return accumulator_us_ >= kTickMicros; }
  void   consume_step()       noexcept { accumulator_us_ -= kTickMicros; }
  double alpha()        const noexcept {
    return static_cast<double>(accumulator_us_) /
           static_cast<double>(kTickMicros);
  }

private:
  int64_t accumulator_us_ = 0;
};
