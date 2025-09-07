#pragma once
#include "state.hpp"

namespace mars {

// Random events entry point (gated by StepOpts.spawn_random_events)
void maybeSpawnRandomEvent(struct GameState& s, const StepOpts& opt);

// Explicit event helpers (used internally / for tests)
void startDustStorm(GameState& s, int hours, const StepOpts& opt);
void clearDustStorm(GameState& s, const StepOpts& opt);
void meteoroidStrike(GameState& s, const StepOpts& opt);
void supplyDrop(GameState& s, const StepOpts& opt);

} // namespace mars
