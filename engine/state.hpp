#pragma once
#include <cstdint>
#include <string>
#include <functional>
#include <random>
#include <vector>
#include <iostream>

namespace mars {

// ---------- Tuning constants (units: kW, kWh, hours) ----------
constexpr int    SOL_HOURS = 24;          // simplify to 24h for now
constexpr double PI        = 3.14159265358979323846;

constexpr double SOLAR_PANEL_KW          = 1.8;  // per panel at daylightFactor=1
constexpr double BATTERY_KWH             = 40.0; // per battery
constexpr double BATTERY_MAX_RATE_KW     = 10.0; // per battery (charge or discharge)
constexpr double LIFE_SUPPORT_BASE_KW    = 1.5;
constexpr double CRIT_PER_COLONIST_KW    = 0.25;
constexpr double LAB_KW                  = 1.2;

// ---------- Typed log / message bus ----------
enum class LogKind { Info, Warning, Event, Weather };

struct LogMsg {
    LogKind     kind;
    std::string text;
};

using LogSink = std::function<void(const LogMsg&)>;

inline void console_sink(const LogMsg& m) {
    // Minimal formatting; UI can get fancier if desired
    std::cout << m.text << '\n';
}
inline void null_sink(const LogMsg&) {}

struct StepOpts {
    bool    spawn_random_events = true;  // forecast sets this to false
    LogSink sink                 = console_sink;
};

// Helper to emit messages safely
inline void emit(const StepOpts& opt, LogKind k, std::string text) {
    if (opt.sink) opt.sink(LogMsg{k, std::move(text)});
}

// ---------- State aggregates ----------
struct Resources {
    double powerStored  = 0.0; // kWh
    double powerCapKWh  = 0.0; // derived: batteries * BATTERY_KWH
};

struct PowerSnapshot {
    double producers        = 0.0;  // kW
    double criticalDemand   = 0.0;  // kW
    double nonCriticalDemand= 0.0;  // kW (total potential)
    double nonCriticalEff   = 0.0;  // 0..1 actually run
    bool   blackout         = false;
};

struct Weather {
    bool   dustStorm        = false;
    int    dustStormHours   = 0;    // remaining
    double solarMultiplier  = 1.0;  // < 1 during storms
};

struct GameState {
    // Time
    int    hour             = 0;    // total hours since start

    // Colony
    int    colonists        = 6;
    int    solarPanels      = 4;
    int    batteries        = 2;
    int    labs             = 1;    // a non-critical consumer

    // Systems
    Resources     res;
    PowerSnapshot lastPower;
    Weather       weather;

    // RNG
    std::mt19937  rng;

    // Helpers
    int hourOfSol() const { return hour % SOL_HOURS; }
    int sol() const { return hour / SOL_HOURS; }
};

// ---------- Setup ----------
inline void recomputePowerCapacity(GameState& s) {
    s.res.powerCapKWh = s.batteries * BATTERY_KWH;
    if (s.res.powerStored > s.res.powerCapKWh) {
        s.res.powerStored = s.res.powerCapKWh;
    }
}

inline void initDefaultGame(GameState& s, uint32_t seed = 42u) {
    s.hour        = 0;
    s.colonists   = 6;
    s.solarPanels = 4;
    s.batteries   = 2;
    s.labs        = 1;

    s.weather = Weather{};
    s.rng.seed(seed);

    recomputePowerCapacity(s);
    s.res.powerStored = s.res.powerCapKWh * 0.5; // start half full
    s.lastPower = PowerSnapshot{};
}

} // namespace mars
