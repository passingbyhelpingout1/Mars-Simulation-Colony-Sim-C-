#include "step.hpp"
#include "power.hpp"
#include "events.hpp"
#include <algorithm>
#include <cmath>

namespace mars {

static double clamp(double x, double lo, double hi) {
    return std::max(lo, std::min(hi, x));
}

void simulateHour(GameState& s, const StepOpts& opt) {
    // 1) Random events (if any)
    maybeSpawnRandomEvent(s, opt);

    // 2) Power production (kW) for this hour
    double day = daylightFactor(s.hourOfSol());
    double solarKW = s.solarPanels * SOLAR_PANEL_KW * day *
                     (s.weather.dustStorm ? s.weather.solarMultiplier : 1.0);

    // 3) Demands (kW)
    double criticalKW = LIFE_SUPPORT_BASE_KW + s.colonists * CRIT_PER_COLONIST_KW;
    double noncritKW  = s.labs * LAB_KW;

    // 4) Discharge to cover critical only
    double availableKW = solarKW;
    double needKW = std::max(0.0, criticalKW - availableKW);
    if (needKW > 0.0) {
        double maxDischarge = s.batteries * BATTERY_MAX_RATE_KW;
        double energyAvailable = std::min(s.res.powerStored, maxDischarge); // 1 hour step
        double discharge = std::min(needKW, energyAvailable);
        s.res.powerStored -= discharge;
        availableKW += discharge;
        needKW = std::max(0.0, criticalKW - availableKW);
    }

    bool blackout = (needKW > 1e-9);
    if (blackout) {
        emit(opt, LogKind::Warning,
             "[Warning] Blackout: critical systems underpowered this hour!");
    }

    // 5) Non-critical policy: ONLY run from surplus (no battery discharge for noncrit)
    double surplusKW = std::max(0.0, availableKW - criticalKW);
    double noncritEff = 0.0;
    if (noncritKW > 1e-9) {
        noncritEff = clamp(surplusKW / noncritKW, 0.0, 1.0);
        surplusKW -= noncritEff * noncritKW;
    }

    // 6) Charge batteries with any leftover surplus
    if (surplusKW > 0.0) {
        double maxCharge = s.batteries * BATTERY_MAX_RATE_KW;
        double room = std::max(0.0, s.res.powerCapKWh - s.res.powerStored);
        double charge = std::min({surplusKW, maxCharge, room});
        s.res.powerStored += charge;
        surplusKW -= charge;
    }

    // 7) Fill snapshot
    s.lastPower.producers         = solarKW;
    s.lastPower.criticalDemand    = criticalKW;
    s.lastPower.nonCriticalDemand = noncritKW;
    s.lastPower.nonCriticalEff    = noncritEff;
    s.lastPower.blackout          = blackout;

    // 8) Advance time
    s.hour += 1;
}

void tickEffects(GameState& s, const StepOpts& opt) {
    // Decrement any active effect durations (dust storm)
    if (s.weather.dustStorm) {
        s.weather.dustStormHours -= 1;
        if (s.weather.dustStormHours <= 0) {
            clearDustStorm(s, opt);
        }
    }
}

Forecast runForecast(GameState& s, int hours) {
    hours = std::max(0, hours);
    Forecast out;
    out.solIndex.reserve(hours);
    out.hourOfSol.reserve(hours);
    out.producers.reserve(hours);
    out.critical.reserve(hours);
    out.noncrit.reserve(hours);
    out.noncritEff.reserve(hours);
    out.battery.reserve(hours);
    out.blackout.reserve(hours);

    // Backup
    GameState backup = s;

    StepOpts opt;
    opt.spawn_random_events = false;
    opt.sink = null_sink; // silent

    for (int i = 0; i < hours; ++i) {
        simulateHour(s, opt);
        tickEffects(s, opt);

        out.solIndex.push_back(s.sol());
        out.hourOfSol.push_back(s.hourOfSol());
        out.producers.push_back(s.lastPower.producers);
        out.critical.push_back(s.lastPower.criticalDemand);
        out.noncrit.push_back(s.lastPower.nonCriticalDemand);
        out.noncritEff.push_back(s.lastPower.nonCriticalEff);
        out.battery.push_back(s.res.powerStored);
        out.blackout.push_back(s.lastPower.blackout ? 1 : 0);
    }

    // Restore
    s = std::move(backup);
    return out;
}

} // namespace mars
