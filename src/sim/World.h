#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <string_view>

namespace sim {

// Keep simulation state POD-ish and deterministic.
// Prefer integers or your fixed_point type; avoid float/double in-world.
struct Colonist {
    uint32_t id;
    int32_t  oxygen_mg;      // example: milligrams
    int32_t  co2_mg;
    int32_t  stress_mil;     // milli-units
    int32_t  temp_milK;      // milli-Kelvin (integer)
};

struct Habitat {
    uint32_t id;
    int32_t  volume_l;       // liters
    int32_t  pressure_mPa;   // milli-Pascal
    int32_t  power_mW;       // milli-Watts available
};

struct World {
    uint64_t tick = 0;
    std::vector<Colonist> colonists;
    std::vector<Habitat>  habitats;

    // Simple, stable checksum over state for tests/replays.
    uint64_t checksum() const noexcept;
    std::string serialize_binary() const; // optional convenience
    static World deserialize_binary(std::string_view bytes);
};

} // namespace sim
