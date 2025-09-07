#include "Simulation.h"

// Example system order. Keep the order *constant* for determinism.
void sim::Simulation::tick(const Input& input) {
    (void)input; // apply inputs here deterministically

    system_power_grid();
    system_life_support();
    system_colonist_needs();

    world_.tick++;
}

void sim::Simulation::system_power_grid() {
    // Example: clamp milli-Watts, avoid floating point
    for (auto& h : world_.habitats) {
        if (h.power_mW < 0) h.power_mW = 0;
    }
}
void sim::Simulation::system_life_support() {
    for (auto& h : world_.habitats) {
        // toy example: pressure bleeds if power low
        if (h.power_mW < 10'000) {
            h.pressure_mPa -= 50; // deterministic integer math
        }
    }
}
void sim::Simulation::system_colonist_needs() {
    for (auto& c : world_.colonists) {
        c.oxygen_mg -= 5; // deterministic consumption per tick
        if (c.oxygen_mg < 0) c.oxygen_mg = 0;
    }
}
