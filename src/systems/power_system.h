// src/systems/power_system.h
#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>

// Keep all power math in Wh for the tick (dtHours), and W for rate-like values.
// The API is intentionally tiny and pure (no globals, no I/O).

namespace mars::power {

struct State {
    double storedWh;      // current energy in battery [Wh]
    double capacityWh;    // battery capacity [Wh]
};

struct Config {
    double etaIn;         // charge efficiency in (0,1]
    double etaOut;        // discharge efficiency in (0,1]
    double cRate;         // C-rate (per hour), i.e., max in/out = cRate * capacityWh per hour
};

struct Inputs {
    double producersW;        // instantaneous producers [W]
    double criticalDemandW;   // must-serve demand [W]
    double nonCriticalDemandW;// can be scaled [W]
    double dtHours;           // time step in hours (typically 1.0)
};

struct Result {
    double nonCriticalEff; // scale in [0,1] actually served for nonCriticalDemand
    double battInWh;       // energy charged during the step [Wh]
    double battOutWh;      // energy discharged during the step [Wh]
    double unmetCriticalWh;// unmet crit energy (should be ~0 after discharge), for diagnostics
};

// Saturating helpers
inline double clamp01(double x) { return std::max(0.0, std::min(1.0, x)); }
inline double saturate(double x, double lo, double hi) { return std::max(lo, std::min(hi, x)); }

// One deterministic step. Pure function: returns new State and fills Result.
State step(State s, const Inputs& in, const Config& cfg, Result& out);

} // namespace mars::power
