#pragma once
#include <cstdint>

class SimClock {
public:
    // 50 Hz sim by default (20 ms tick). Adjust to your needs.
    static constexpr int64_t kTickMicros = 20'000;

    void advance_by_frame_us(int64_t frame_dt_us) {
        if (frame_dt_us < 0) frame_dt_us = 0;
        // Clamp pathological long frames so we don't spiral.
        if (frame_dt_us > 250'000) frame_dt_us = 250'000; // 250 ms cap
        accumulator_us_ += frame_dt_us;
    }

    bool step_ready() const { return accumulator_us_ >= kTickMicros; }
    void consume_step()     { accumulator_us_ -= kTickMicros; }

    // Interpolation factor for presentation (0..1) — use only on render/UI side.
    double alpha() const {
        return static_cast<double>(accumulator_us_) / static_cast<double>(kTickMicros);
    }

private:
    int64_t accumulator_us_ = 0;
};
