// sim.cpp
#include "sim.hpp"
#include <iostream>

void Game::step() {
    // Apply commands scheduled for THIS hour before events/simulation (matches your current order)
    orders.drain_for_hour(s.hour, [&](const Command& c){
        if (c.type == CommandType::Build) {
            tryBuild(static_cast<BuildingType>(c.a));
        }
    });
    maybeSpawnEvents();
    simulateHour();
    tickEffects();
    ++s.hour;
}
