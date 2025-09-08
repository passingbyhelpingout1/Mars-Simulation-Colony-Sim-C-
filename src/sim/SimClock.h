#pragma once
#include <cstdint>

class SimClock {
public:
    // step_us: simulation step duration (microseconds), e.g. 100'000 = 10 Hz
    explicit SimClock(std::uint64_t step_us = 100'000)  // default 10 Hz
        : step_us_(step_us), acc_us_(0), tick_(0), speed_q16_(1u << 16) {}

    // 1.0x speed == 65536. Use this to avoid floating-point in the hot path.
    void set_speed_q16(std::uint32_t q16) { speed_q16_ = q16; }

    // Convenience for UI/debug; rounds to Q16.16.
    void set_speed(double s) {
        if (s < 0) s = 0;
        if (s > 16) s = 16;
        speed_q16_ = static_cast<std::uint32_t>(s * 65536.0 + 0.5);
    }

    // Advance the clock by wall-clock microseconds since the last frame.
    inline void advance_by_frame_us(std::uint64_t frame_us) {
        // (frame_us * speed_q16) >> 16 — all integer math
        acc_us_ += (frame_us * static_cast<std::uint64_t>(speed_q16_)) >> 16;
    }

    inline bool step_ready() const { return acc_us_ >= step_us_; }

    inline void consume_step() {
        acc_us_ -= step_us_;
        ++tick_;
    }

    inline std::uint64_t tick()    const { return tick_;    }
    inline std::uint64_t step_us() const { return step_us_; }
    inline std::uint64_t acc_us()  const { return acc_us_;  }

private:
    std::uint64_t step_us_;
    std::uint64_t acc_us_;
    std::uint64_t tick_;
    std::uint32_t speed_q16_;  // 1.0x == 65536
};
