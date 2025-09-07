#include <iostream>
#include <iomanip>
#include <string>
#include <limits>
#include <vector>
#include <algorithm>

#include "../../engine/state.hpp"
#include "../../engine/step.hpp"
#include "../../engine/power.hpp"
#include "../../engine/persist.hpp"

using namespace mars;

static int readInt(const std::string& prompt, int lo, int hi) {
    while (true) {
        std::cout << prompt;
        int v;
        if (std::cin >> v && v >= lo && v <= hi) {
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            return v;
        }
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "Enter a number in [" << lo << ", " << hi << "].\n";
    }
}

static void showStatus(const GameState& s) {
    std::cout << "\n=== Colony Status ===\n";
    std::cout << "Time: Sol " << s.sol() << ", Hour " << s.hourOfSol() << " (T+" << s.hour << "h)\n";
    std::cout << "Colonists: " << s.colonists << "\n";
    std::cout << "Installations: " << s.solarPanels << " Solar, " << s.batteries << " Batteries, "
              << s.labs << " Lab(s)\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Battery: " << s.res.powerStored << " / " << s.res.powerCapKWh << " kWh\n";
    std::cout << "Weather: " << (s.weather.dustStorm ? "Dust storm (" + std::to_string(s.weather.dustStormHours) + "h left)"
                                                     : "Clear") << "\n";

    // Quick estimates for current hour
    double day = daylightFactor(s.hourOfSol()) * (s.weather.dustStorm ? s.weather.solarMultiplier : 1.0);
    double solarKW = s.solarPanels * SOLAR_PANEL_KW * day;
    double critKW  = LIFE_SUPPORT_BASE_KW + s.colonists * CRIT_PER_COLONIST_KW;
    double nonKW   = s.labs * LAB_KW;

    std::cout << "Now (estimates): Gen " << solarKW << " kW, Critical " << critKW
              << " kW, Non-crit potential " << nonKW << " kW\n";
    if (s.lastPower.criticalDemand > 0.0) {
        std::cout << "Last hour: Gen " << s.lastPower.producers << " kW, Critical "
                  << s.lastPower.criticalDemand << " kW, Non-crit run "
                  << (100.0 * s.lastPower.nonCriticalEff) << "%, "
                  << (s.lastPower.blackout ? "BLACKOUT" : "ok") << "\n";
    }
    std::cout << "=====================\n\n";
}

static void doAdvance(GameState& s, int hours) {
    StepOpts opt; // default: random events on, console sink
    for (int i = 0; i < hours; ++i) {
        simulateHour(s, opt);
        tickEffects(s, opt);
    }
}

static void doBuild(GameState& s) {
    std::cout << "\nBuild what?\n"
                 " 1) Solar panel (+" << SOLAR_PANEL_KW << " kW peak)\n"
                 " 2) Battery (+" << BATTERY_KWH << " kWh, ±" << BATTERY_MAX_RATE_KW << " kW rate)\n"
                 " 3) Lab (+" << LAB_KW << " kW non-critical)\n";
    int c = readInt("Choice: ", 1, 3);
    if (c == 1) {
        s.solarPanels += 1;
        emit(StepOpts{}, LogKind::Info, "[Build] Installed +1 Solar panel.");
    } else if (c == 2) {
        s.batteries += 1;
        recomputePowerCapacity(s);
        emit(StepOpts{}, LogKind::Info, "[Build] Installed +1 Battery.");
    } else {
        s.labs += 1;
        emit(StepOpts{}, LogKind::Info, "[Build] Commissioned +1 Lab (non-critical).");
    }
}

static void doForecast(GameState& s, int hours) {
    auto f = runForecast(s, hours);
    std::cout << "\n--- Power forecast (" << hours << "h, no events) ---\n";
    std::cout << std::left << std::setw(8) << "T+Hr"
              << std::setw(8) << "Sol"
              << std::setw(8) << "Hour"
              << std::setw(10) << "Gen(kW)"
              << std::setw(10) << "Crit(kW)"
              << std::setw(10) << "Ncrit(kW)"
              << std::setw(10) << "Run(%)"
              << std::setw(12) << "Batt(kWh)"
              << std::setw(8) << "Blkout"
              << "\n";

    int t = 1;
    for (size_t i = 0; i < f.producers.size(); ++i, ++t) {
        std::cout << std::left << std::setw(8) << t
                  << std::setw(8) << f.solIndex[i]
                  << std::setw(8) << f.hourOfSol[i]
                  << std::setw(10) << std::fixed << std::setprecision(2) << f.producers[i]
                  << std::setw(10) << f.critical[i]
                  << std::setw(10) << f.noncrit[i]
                  << std::setw(10) << (100.0 * f.noncritEff[i])
                  << std::setw(12) << f.battery[i]
                  << std::setw(8) << (f.blackout[i] ? "YES" : "no")
                  << "\n";
    }
    std::cout << "----------------------------------------------\n\n";
}

static void doSave(const GameState& s) {
    std::string path = "save.txt";
    if (saveGame(s, path)) {
        std::cout << "Saved to " << path << "\n";
    } else {
        std::cout << "Failed to save.\n";
    }
}

static void doLoad(GameState& s) {
    std::string path = "save.txt";
    if (loadGame(s, path)) {
        std::cout << "Loaded from " << path << "\n";
    } else {
        std::cout << "Failed to load.\n";
    }
}

int main() {
    GameState s;
    initDefaultGame(s, 42u);

    std::cout << "=== Mars Simulation (CLI) ===\n";
    bool running = true;
    while (running) {
        std::cout << "\nMenu:\n"
                     " 1) Advance 1 hour\n"
                     " 2) Advance 6 hours\n"
                     " 3) Power forecast (24h)\n"
                     " 4) Build (Solar/Battery/Lab)\n"
                     " 5) Status\n"
                     " 6) Save\n"
                     " 7) Load\n"
                     " 0) Quit\n";
        int c = readInt("Choice: ", 0, 7);
        switch (c) {
            case 1: doAdvance(s, 1); break;
            case 2: doAdvance(s, 6); break;
            case 3: doForecast(s, 24); break;
            case 4: doBuild(s); break;
            case 5: showStatus(s); break;
            case 6: doSave(s); break;
            case 7: doLoad(s); break;
            case 0: running = false; break;
        }
    }
    std::cout << "Goodbye.\n";
    return 0;
}
