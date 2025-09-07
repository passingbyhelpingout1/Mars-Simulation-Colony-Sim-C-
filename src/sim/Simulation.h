#pragma once
#include "World.h"
#include "Rng.h"

namespace sim {

struct Input {
    // Populate with your actual inputs for a tick (UI/AI orders, etc.)
    // Keep it deterministic & serializable (integers/enums).
    int32_t example_command = 0;
};

class Simulation {
public:
    explicit Simulation(uint64_t seed = 0) : rng_(seed) {}
    World& world()       { return world_; }
    const World& world() const { return world_; }

    // advance exactly one tick
    void tick(const Input& input);

private:
    World world_;
    Rng   rng_;

    // split your systems into private helpers; call them in a fixed order
    void system_life_support();
    void system_colonist_needs();
    void system_power_grid();
};

} // namespace sim
