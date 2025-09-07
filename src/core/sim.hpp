// sim.hpp
#pragma once
#include "state.hpp"
#include "command.hpp"
#include "specs.hpp"

class Game {
public:
    GameState s;
    bool forecastMode{false}; // you already use this knob

    // Single step: applies commands for this hour, spawns events, runs sim, ticks effects.
    void step();

    // Public API—thin wrappers you already have
    bool tryBuild(BuildingType t);
    void submit(const Command& c) { orders.submit(c); }

private:
    CommandQueue orders;
    void maybeSpawnEvents();  // move your existing logic here
    void simulateHour();      // “power, life support, greenhouse, etc.”
    void tickEffects();       // post-step effects
};
