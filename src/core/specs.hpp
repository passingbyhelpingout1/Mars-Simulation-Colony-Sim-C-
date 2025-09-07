#pragma once
#include <array>
#include "types.hpp"

struct BuildingSpec {
    // Fill with what you already have: costs, power in/out, crew req, etc.
    int metals_cost;
    int credits_cost;
    double power_out_kw;   // +ve generation, -ve consumption
    double upkeep_kw;      // maintenance / standby
    // ... any life support deltas, greenhouse yields, etc.
};

inline constexpr std::array<BuildingSpec, to_index(BuildingType::COUNT)> SPEC {{
    /* Solar      */ BuildingSpec{ /*metals*/10, /*credits*/500, /*power*/ +2.0, /*upkeep*/0.0 },
    /* Battery    */ BuildingSpec{ 8,  400, /*power*/ 0.0, 0.02 },
    /* Habitat    */ BuildingSpec{ 20, 800, -1.2, 0.10 },
    /* Greenhouse */ BuildingSpec{ 18, 700, -0.8, 0.10 },
    /* Extractor  */ BuildingSpec{ 25, 900, -1.0, 0.15 },
    /* Reactor    */ BuildingSpec{ 50, 2500, +6.0, 0.25 },
}};

inline const BuildingSpec& getSpec(BuildingType t) {
    return SPEC[to_index(t)]; // avoids map lookups/at() throws
}
