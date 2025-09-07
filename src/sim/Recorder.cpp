// Recorder.cpp
#include "Recorder.h"
#include <fstream>

using namespace sim;

void Recorder::push(uint64_t tick, const Input& in) { events_.emplace_back(tick, in); }

bool Recorder::save(const std::string& file) const {
    std::ofstream f(file, std::ios::binary);
    if (!f) return false;
    uint64_t n = events_.size();
    f.write((const char*)&n, sizeof n);
    for (auto& [t, e] : events_) {
        f.write((const char*)&t, sizeof t);
        f.write((const char*)&e, sizeof e);
    }
    return true;
}

bool Recorder::load(const std::string& file) {
    std::ifstream f(file, std::ios::binary);
    if (!f) return false;
    uint64_t n = 0;
    f.read((char*)&n, sizeof n);
    events_.resize(n);
    for (auto& [t, e] : events_) {
        f.read((char*)&t, sizeof t);
        f.read((char*)&e, sizeof e);
    }
    return true;
}
