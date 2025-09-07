// Recorder.h
#pragma once
#include "Simulation.h"
#include <vector>
#include <cstdint>
#include <string>

namespace sim {
class Recorder {
public:
    void push(uint64_t tick, const Input& in);
    bool save(const std::string& file) const;
    bool load(const std::string& file);
    const std::vector<std::pair<uint64_t, Input>>& events() const { return events_; }

private:
    std::vector<std::pair<uint64_t, Input>> events_;
};
} // namespace sim
