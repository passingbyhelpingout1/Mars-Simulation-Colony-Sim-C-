#include "events.hpp"
#include <algorithm>
#include <random>
#include <sstream>

namespace mars {

static double rand01(std::mt19937& rng) {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng);
}

static int randInt(std::mt19937& rng, int lo, int hi) { // inclusive
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(rng);
}

void startDustStorm(GameState& s, int hours, const StepOpts& opt) {
    if (s.weather.dustStorm) return;
    s.weather.dustStorm = true;
    s.weather.dustStormHours = std::max(1, hours);
    s.weather.solarMultiplier = 0.25; // strong attenuation
    emit(opt, LogKind::Weather, "[Weather] A dust storm rolls in. Solar output reduced.");
}

void clearDustStorm(GameState& s, const StepOpts& opt) {
    if (!s.weather.dustStorm) return;
    s.weather = Weather{}; // reset to defaults
    emit(opt, LogKind::Weather, "[Weather] The dust storm has cleared.");
}

void meteoroidStrike(GameState& s, const StepOpts& opt) {
    if (s.solarPanels <= 0) return;
    s.solarPanels = std::max(0, s.solarPanels - 1);
    recomputePowerCapacity(s);
    emit(opt, LogKind::Event, "[Event] Meteoroid strike! A solar panel was destroyed.");
}

void supplyDrop(GameState& s, const StepOpts& opt) {
    // 50/50: add a battery or a solar panel
    bool addBattery = (rand01(s.rng) < 0.5);
    if (addBattery) {
        s.batteries += 1;
        recomputePowerCapacity(s);
        emit(opt, LogKind::Event, "[Event] Orbital supply drop delivered: +1 Battery module.");
    } else {
        s.solarPanels += 1;
        emit(opt, LogKind::Event, "[Event] Orbital supply drop delivered: +1 Solar panel.");
    }
}

void maybeSpawnRandomEvent(GameState& s, const StepOpts& opt) {
    if (!opt.spawn_random_events) return;

    // Gentle probabilities per hour; tune to taste
    // - Dust storm: ~0.5%/hr when not already storming
    // - Meteoroid: ~0.15%/hr (rare)
    // - Supply drop: ~0.20%/hr (occasional)
    if (!s.weather.dustStorm && rand01(s.rng) < 0.005) {
        int hrs = randInt(s.rng, 12, 48); // 0.5–2 sols
        startDustStorm(s, hrs, opt);
    }
    if (rand01(s.rng) < 0.0015) {
        meteoroidStrike(s, opt);
    }
    if (rand01(s.rng) < 0.0020) {
        supplyDrop(s, opt);
    }
}

} // namespace mars
