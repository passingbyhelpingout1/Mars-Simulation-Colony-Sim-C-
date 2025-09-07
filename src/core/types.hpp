#pragma once
#include <cstdint>
#include <cassert>
#include <string_view>

// Use strong, explicit units to avoid “hours vs days” bugs.
using Hours = long long;

enum class BuildingType : uint8_t {
    Solar, Battery, Habitat, Greenhouse, Extractor, Reactor,
    COUNT
};

constexpr size_t to_index(BuildingType t) {
    return static_cast<size_t>(t);
}

inline std::string_view to_string(BuildingType t) {
    switch (t) {
        case BuildingType::Solar:      return "Solar";
        case BuildingType::Battery:    return "Battery";
        case BuildingType::Habitat:    return "Habitat";
        case BuildingType::Greenhouse: return "Greenhouse";
        case BuildingType::Extractor:  return "Extractor";
        case BuildingType::Reactor:    return "Reactor";
        default:                       return "Unknown";
    }
}
