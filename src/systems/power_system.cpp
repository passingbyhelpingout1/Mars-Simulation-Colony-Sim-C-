// src/systems/power_system.cpp
#include "power_system.h"

namespace mars::power {

State step(State s, const Inputs& in, const Config& cfg, Result& out) {
    // Convert W -> Wh for the tick
    const double prodWh   = in.producersW       * in.dtHours;
    const double critWh   = in.criticalDemandW  * in.dtHours;
    const double nonCritWh= in.nonCriticalDemandW * in.dtHours;

    // C-rate limits for this step
    const double maxInWh  = cfg.cRate * s.capacityWh * in.dtHours;
    const double maxOutWh = cfg.cRate * s.capacityWh * in.dtHours;

    double battInWh  = 0.0;
    double battOutWh = 0.0;

    // Serve critical first: use producers, then discharge battery
    double availableWh = prodWh;
    double unmetCriticalWh = 0.0;

    if (availableWh >= critWh) {
        availableWh -= critWh; // critical fully served
    } else {
        // Need discharge
        double needWh = critWh - availableWh;
        // Due to discharge efficiency, we must draw more from the pack
        double drawWh = std::min({ needWh / cfg.etaOut, maxOutWh, s.storedWh });
        battOutWh = drawWh;
        availableWh += drawWh * cfg.etaOut;

        if (availableWh >= critWh) {
            availableWh -= critWh;
        } else {
            // Even after max discharge, still not enough
            unmetCriticalWh = critWh - availableWh;
            availableWh = 0.0;
        }
    }

    // Non-critical: whatever remains charges nonCritical, possibly scale
    double serveNonCritWh = std::min(availableWh, nonCritWh);
    double nonCritEff = (nonCritWh > 0.0) ? clamp01(serveNonCritWh / nonCritWh) : 1.0;

    // If producers exceed all demand, try to charge battery with the spare
    double spareWh = availableWh - serveNonCritWh;
    if (spareWh > 1e-12) {
        // Account for charge efficiency: to store X, we must input X/etaIn from spare
        // But we are limited by capacity room and C-rate.
        const double roomWh = std::max(0.0, s.capacityWh - s.storedWh);
        // Energy that can actually be stored this step:
        double storeWh = std::min({ spareWh * cfg.etaIn, roomWh, maxInWh * cfg.etaIn });
        // Convert stored energy back to input used from spare
        double usedSpareWh = storeWh / cfg.etaIn;
        // If rounding pushed beyond spare, clamp
        storeWh = std::min(storeWh, spareWh * cfg.etaIn);
        battInWh = storeWh; // actual stored energy
        spareWh -= usedSpareWh;
    }

    // Update SoC with saturating math
    s.storedWh = saturate(s.storedWh + battInWh - battOutWh, 0.0, s.capacityWh);

    out.nonCriticalEff   = clamp01(nonCritEff);
    out.battInWh         = battInWh;
    out.battOutWh        = battOutWh;
    out.unmetCriticalWh  = unmetCriticalWh;
    return s;
}

} // namespace mars::power
