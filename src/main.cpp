#include "sim/SimClock.h"
#include "sim/Simulation.h"
#include "sim/Recorder.h"
#include <chrono>

static int64_t now_us() {
    using clock = std::chrono::steady_clock;
    static auto start = clock::now();
    auto dt = clock::now() - start;
    return std::chrono::duration_cast<std::chrono::microseconds>(dt).count();
}

int main() {
    sim::Simulation simulation(/*seed=*/12345);
    sim::Recorder   recorder;

    // bootstrap a world (example)
    auto& w = simulation.world();
    w.colonists.push_back({1, 20000, 0, 0, 293'000}); // 293 K
    w.habitats.push_back({1, 50000, 101'325'000, 15'000});

    SimClock clock;
    int64_t last = now_us();

    while (true) {
        int64_t t = now_us();
        clock.advance_by_frame_us(t - last);
        last = t;

        while (clock.step_ready()) {
            sim::Input in{};
            // TODO: read UI/CLI input deterministically into 'in'
            recorder.push(w.tick, in);
            simulation.tick(in);
            clock.consume_step();
        }

        // TODO: render using clock.alpha() if you interpolate between last/cur states

        // Temporary exit condition for example builds
        if (w.tick > 2000) break;
    }

    recorder.save("replay.bin");
    return 0;
}
