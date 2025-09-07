#pragma once
#include "state.hpp"
#include <vector>

namespace mars {

struct Forecast {
    std::vector<int>    solIndex;     // sol number at each step
    std::vector<int>    hourOfSol;    // hour in sol
    std::vector<double> producers;    // kW
    std::vector<double> critical;     // kW
    std::vector<double> noncrit;      // kW potential
    std::vector<double> noncritEff;   // 0..1 actually served
    std::vector<double> battery;      // kWh
    std::vector<uint8_t> blackout;    // 0/1
};

void simulateHour(GameState& s, const StepOpts& opt);
void tickEffects(GameState& s, const StepOpts& opt);

// Run N silent hours with no random events; return series and restore state
Forecast runForecast(GameState& s, int hours);

} // namespace mars
