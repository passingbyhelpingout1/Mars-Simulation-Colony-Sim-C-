#pragma once
#include <map>
#include <vector>
#include "types.hpp"

enum class CommandType { Build /*, Toggle, etc.*/ };

struct Command {
    Hours hour{0};
    CommandType type{CommandType::Build};
    int a{0}; // For Build: static_cast<int>(BuildingType)
};

// Simple time-indexed queue; keeps your “apply at hour start” rule.
struct CommandQueue {
    std::multimap<Hours, Command> pending;
    void submit(const Command& c) { pending.emplace(c.hour, c); }
    // iterate [hour, hour] range and apply
    template <class Fn>
    void drain_for_hour(Hours h, Fn&& apply) {
        auto range = pending.equal_range(h);
        for (auto it = range.first; it != range.second; ++it) {
            apply(it->second);
        }
        pending.erase(range.first, range.second);
    }
};
