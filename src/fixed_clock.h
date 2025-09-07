#pragma once
#include <chrono>

class FixedClock {
public:
    using Clock = std::chrono::steady_clock;
    explicit FixedClock(double dt_seconds = 1.0 / 20.0, double max_frame = 0.25)
        : dt(dt_seconds), maxFrame(max_frame) { reset(); }

    void reset() { last = Clock::now(); acc = 0.0; }

    // Advance time, return how many fixed steps to simulate this frame
    int advance() {
        auto now = Clock::now();
        double frame = std::chrono::duration<double>(now - last).count();
        if (frame > maxFrame) frame = maxFrame;         // avoid spiral of death
        last = now;
        acc += frame;
        int steps = 0;
        while (acc >= dt) { acc -= dt; ++steps; }
        return steps;
    }

    double alpha() const { return acc / dt; }           // for interpolation if rendering
    double step()  const { return dt; }

private:
    double dt, maxFrame, acc = 0.0;
    Clock::time_point last;
};
