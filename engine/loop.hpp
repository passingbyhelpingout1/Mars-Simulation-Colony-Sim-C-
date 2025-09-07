#pragma once
#include "time.hpp"

// Your game exposes these (or adapt names):
struct Game;          // holds world/sim state
struct InputFrame;    // per-frame input snapshot
void poll_input(InputFrame&);             // fill from OS/UI
void sim_update(Game&, double fixed_dt);  // advance sim by fixed_dt seconds
void render_frame(const Game&, double alpha); // alpha in [0,1), for interpolation

namespace eng {

// Run a deterministic, accumulator-based fixed-timestep loop (Gaffer pattern).
inline void run(Game& game) {
  using namespace std::chrono;
  auto t0 = clock::now();
  double accumulator = 0.0;

  InputFrame input{};
  bool running = true;

  while (running) {
    // 1) Measure frame time using a monotonic clock
    auto t1 = clock::now();
    double frame_dt = duration_cast<seconds_f64>(t1 - t0).count();
    t0 = t1;

    // Optional: clamp if the app was minimized or stalled to avoid spiral-of-death
    if (frame_dt > 0.25) frame_dt = 0.25;

    accumulator += frame_dt;

    // 2) Gather inputs (don’t apply yet)
    poll_input(input);
    if (/* input requests quit */ false) { running = false; }

    // 3) Step the simulation in fixed quanta (e.g., 50 ms)
    while (accumulator >= kFixedDt) {
      sim_update(game, kFixedDt);
      accumulator -= kFixedDt;
    }

    // 4) Render with interpolation factor alpha for smoothness
    const double alpha = accumulator / kFixedDt;
    render_frame(game, alpha);
  }
}

} // namespace eng
