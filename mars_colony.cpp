/*
  Mars Colony — Windows-friendly starter simulation (C++17, single-file)

  Key improvements for Windows:
   • Robust menu input (line-based), no silent exit on bad input.
   • Optional CLI flags: --autorun N, --headless N, --no-pause, --seed U32, --load FILE, --save FILE.
   • Try/catch with clear error messages instead of sudden close.
   • Text simplified to ASCII for maximum console compatibility.
   • Deterministic save/load (versioned text format) + reproducible RNG seeds.
   • Discrete non-critical power dispatch via a tiny 0/1 knapsack optimizer.

  NEW in this build:
   • Physically-plausible battery model with C-rate limits and round-trip efficiency.
   • Save/Load v2 (backward compatible with v1) to persist new battery parameters.
   • Power forecast shows scheduled charge/discharge totals.
   • Status panel shows battery model parameters.
   • **Deterministic command log (record/replay of build orders).**
     - CLI: --record FILE (write header + subsequent orders), --replay FILE (load and schedule).
     - Orders are applied at the start of each hour before weather and sim.
     - Forecasts include pending orders by simulating on a cloned state/queue.
   • **Simulation invariants + headless self-test + CI-friendly flags.**
     - CLI: --check-invariants (throw on invariant failure each hour), --selftest (run deterministic test).
     - Invariants catch NaNs/negatives, SoC bounds, eta/C-rate ranges, etc.
     - Self-test abuses forecast and save/load, returns non-zero on failure.

  Build (MinGW-w64 g++):
    g++ -std=c++17 -O2 -Wall -Wextra -o MarsColony.exe mars_colony.cpp
*/

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <map>
#include <random>
#include <algorithm>
#include <functional>
#include <limits>
#include <chrono>
#include <sstream>
#include <fstream>
#include <cstdint>
#include <cmath>
#include <numeric>
#include <stdexcept>

using std::cout;
using std::cin;
using std::string;
using std::vector;

// ----------- Utility ---------------------------------------------------------

template <typename T>
T clampv(T v, T lo, T hi) { return std::min(hi, std::max(lo, v)); }

static inline string pluralize(const string& word, int n) {
    return word + (n == 1 ? "" : "s");
}

static int readInt(const string& prompt, int minVal, int maxVal) {
    while (true) {
        cout << prompt;
        string line;
        if (!std::getline(cin >> std::ws, line)) {
            // input stream closed; keep running but return 0 (quit)
            return 0;
        }
        std::stringstream ss(line);
        int v;
        if ((ss >> v) && v >= minVal && v <= maxVal) return v;
        cout << "Please enter a number between " << minVal << " and " << maxVal << ".\n";
    }
}

static string readLine(const string& prompt, const string& defValue) {
    cout << prompt << " [" << defValue << "]: ";
    string line;
    if (!std::getline(cin >> std::ws, line) || line.empty()) return defValue;
    return line;
}

// ----------- Core types ------------------------------------------------------

enum class BuildingType {
    SolarArray,
    BatteryBank,
    Habitat,
    Greenhouse,
    WaterExtractor,
    Electrolyzer,
    RTG
};

static inline string to_string(BuildingType t) {
    switch (t) {
        case BuildingType::SolarArray:    return "Solar Array";
        case BuildingType::BatteryBank:   return "Battery Bank";
        case BuildingType::Habitat:       return "Habitat";
        case BuildingType::Greenhouse:    return "Greenhouse";
        case BuildingType::WaterExtractor:return "Water Extractor";
        case BuildingType::Electrolyzer:  return "Electrolyzer";
        case BuildingType::RTG:           return "RTG";
    }
    return "Unknown";
}

struct BuildingSpec {
    string name;
    // Power characteristics (per hour)
    double powerProdDay   = 0.0; // solar (scaled by daylight & storms)
    double powerProdConst = 0.0; // RTG constant output
    double powerCons      = 0.0; // consumption when active

    // Resource flows (per hour, + = production)
    double waterFlow  = 0.0;
    double oxygenFlow = 0.0;
    double foodFlow   = 0.0;

    // Other effects
    int    housing = 0;
    double batteryCapacityDelta = 0.0;

    // Build costs
    int metalsCost   = 0;
    int creditsCost  = 0;

    bool needsPower     = false;
    bool isCriticalLoad = false;
};

struct Building {
    BuildingType type;
    bool active = true;
};

enum class EffectType { DustStorm };

struct ActiveEffect {
    EffectType type;
    int hoursRemaining = 0;
    double solarMultiplier = 1.0;
    string description;
};

struct ColonyResources {
    double powerStored = 300.0;
    double batteryCapacity = 600.0;
    double water  = 100.0;
    double oxygen = 200.0;
    double food   = 100.0;
    int metals  = 200;
    int credits = 1000;
};

struct LastPowerReport {
    double producers = 0.0;
    double criticalDemand = 0.0;
    double nonCriticalDemand = 0.0;
    double nonCriticalEff = 0.0; // share of non-critical demand actually run [0..1]
    bool blackout = false;

    // NEW: battery telemetry for the last simulated hour
    double battIn  = 0.0; // kWh taken from producers into the battery (charge input energy)
    double battOut = 0.0; // kWh delivered from battery to loads (after discharge efficiency)
    bool   chargeCRateLimited    = false;
    bool   dischargeCRateLimited = false;
};

// ----------- Specs database --------------------------------------------------

static const BuildingSpec& getSpec(BuildingType t) {
    static const std::map<BuildingType, BuildingSpec> DB = {
        { BuildingType::SolarArray,
          BuildingSpec{ "Solar Array", 25.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0, 0.0, 50, 100, false, false } },
        { BuildingType::BatteryBank,
          BuildingSpec{ "Battery Bank", 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0, 200.0, 40, 50, false, false } },
        { BuildingType::Habitat,
          BuildingSpec{ "Habitat", 0.0, 0.0, 2.0, 0.0, 0.0, 0.0, 5, 0.0, 100, 500, true, true } },
        { BuildingType::Greenhouse,
          BuildingSpec{ "Greenhouse", 0.0, 0.0, 12.0, -2.0, 1.0, 2.0, 0, 0.0, 80, 400, true, false } },
        { BuildingType::WaterExtractor,
          BuildingSpec{ "Water Extractor", 0.0, 0.0, 8.0, 3.0, 0.0, 0.0, 0, 0.0, 60, 300, true, false } },
        { BuildingType::Electrolyzer,
          BuildingSpec{ "Electrolyzer", 0.0, 0.0, 10.0, -1.0, 1.5, 0.0, 0, 0.0, 50, 350, true, false } },
        { BuildingType::RTG,
          BuildingSpec{ "RTG", 0.0, 30.0, 0.0, 0.0, 0.0, 0.0, 0, 0.0, 200, 2000, false, false } },
    };
    return DB.at(t);
}

// ----------- Commands (event-sourced) ---------------------------------------

enum class CommandType { Build /*, Toggle could be added later with IDs*/ };

struct Command {
    long long hour = 0;         // when to apply (start of hour)
    CommandType type{};
    int a = 0;                  // payload: for Build -> static_cast<int>(BuildingType)
};

// ----------- Simulation ------------------------------------------------------

struct GameState {
    long long hour = 0;
    int population = 5;
    int housingCapacity = 5;
    double morale = 0.75; // 0..1

    ColonyResources res;
    vector<Building> buildings;
    vector<ActiveEffect> effects;
    LastPowerReport lastPower;

    // NEW: Battery model parameters (tunable)
    double batteryCRate  = 0.50; // per-hour C-rate (0.5C -> can move 0.5 * capacity per hour)
    double batteryEtaIn  = 0.92; // charge efficiency
    double batteryEtaOut = 0.95; // discharge efficiency

    std::mt19937 rng;
    uint32_t rngSeed = 0; // recorded for reproducibility

    GameState() {
        unsigned seed = 0u;
        try {
            std::random_device rd;
            seed = rd();
        } catch (...) {
            seed = static_cast<unsigned>(
                std::chrono::high_resolution_clock::now().time_since_epoch().count()
            );
        }
        rngSeed = static_cast<uint32_t>(seed);
        rng.seed(seed);

        // (explicit defaults already set above)
    }
};

class Game {
public:
    Game() { initStarter(); }

    void setSeed(uint32_t seed) {
        s.rngSeed = seed;
        s.rng.seed(seed);
    }

    // Enable/disable throwing on invariant failures each simulated hour.
    void enableHardInvariants(bool on) { hardFailOnInvariant = on; }

    // Run a deterministic, headless self-test for CI.
    // Returns 0 on success, non-zero on failure.
    int runSelfTest() {
        // Build a separate Game instance so we don't disturb *this*
        Game g;
        g.enableHardInvariants(true);
        g.setSeed(123456789u);

        // 1) Advance and place a couple of buildings deterministically.
        g.advanceHours(24);
        g.queueBuildNow(BuildingType::SolarArray);
        g.advanceHours(48);
        g.queueBuildNow(BuildingType::BatteryBank);
        g.advanceHours(24);

        // 2) Forecast must be non-destructive (state unchanged afterwards).
        auto before = g;              // deep copy (allowed for members)
        g.forecastHours(72);          // private method; accessible within class
        auto after = g;

        auto feq = [](double a, double b){ return std::fabs(a - b) <= 1e-9; };
        bool same =
            (before.s.hour == after.s.hour) &&
            feq(before.s.res.powerStored, after.s.res.powerStored) &&
            feq(before.s.res.water,       after.s.res.water) &&
            feq(before.s.res.oxygen,      after.s.res.oxygen) &&
            feq(before.s.res.food,        after.s.res.food);

        if (!same) {
            cout << "[SelfTest] forecastHours mutated state.\n";
            return 2;
        }

        // 3) Save/Load round-trip + continue sim should not trip invariants.
        const char* tmp = "selftest.mc";
        if (!g.saveToFile(tmp)) return 3;

        Game g2;
        g2.enableHardInvariants(true);
        if (!g2.loadFromFile(tmp)) return 4;
        g2.advanceHours(24); // will throw if invariants fail

        cout << "[SelfTest] OK\n";
        return 0;
    }

    // --- Recording / Replay (public API) ------------------------------------

    // Start writing commands to a file. Writes/overwrites a header immediately.
    bool startRecordingTo(const string& path) {
        std::ofstream ofs(path, std::ios::out | std::ios::trunc);
        if (!ofs) { cout << "Failed to open '" << path << "' for recording.\n"; return false; }
        ofs << "MARS_REPLAY 1\n";
        ofs << "seed " << s.rngSeed << "\n";
        ofs << "start_hour " << s.hour << "\n";
        ofs << "endheader\n";
        ofs.close();
        recordPath = path;
        recording = true;
        recordHeaderWritten = true;
        cout << "Recording orders to '" << recordPath << "'.\n";
        return true;
    }

    // Load a replay file and enqueue its commands. If it contains a seed and
    // allowSeedOverride is true (and no save was loaded), apply that seed now.
    bool loadReplayFile(const string& path, bool allowSeedOverride, bool userProvidedSeedOrSaveAlready) {
        std::ifstream ifs(path, std::ios::in);
        if (!ifs) { cout << "Failed to open replay '" << path << "'.\n"; return false; }

        string tag; int version = 0;
        if (!(ifs >> tag >> version) || tag != "MARS_REPLAY" || version != 1) {
            cout << "Unrecognized replay header.\n"; return false;
        }
        // consume rest of line
        ifs.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        uint32_t replSeed = 0;
        bool hasSeed = false;
        long long startHour = 0;
        bool inHeader = true;
        size_t loaded = 0;

        string line;
        while (std::getline(ifs, line)) {
            if (line.empty()) continue;
            std::istringstream iss(line);
            if (inHeader) {
                string k; iss >> k;
                if (k == "seed") {
                    iss >> replSeed; hasSeed = true;
                } else if (k == "start_hour") {
                    iss >> startHour;
                } else if (k == "endheader") {
                    inHeader = false;
                }
                continue;
            }

            // Commands section
            // Formats supported:
            //  "h <hour> build <typeInt>"
            //  "build <hour> <typeInt>"
            string k; iss >> k;
            if (k == "h") {
                long long h; string what; int t;
                if (iss >> h >> what >> t) {
                    if (what == "build") {
                        Command c; c.hour = h; c.type = CommandType::Build; c.a = t;
                        pendingCommands.emplace(c.hour, c);
                        ++loaded;
                    }
                }
            } else if (k == "build") {
                long long h; int t;
                if (iss >> h >> t) {
                    Command c; c.hour = h; c.type = CommandType::Build; c.a = t;
                    pendingCommands.emplace(c.hour, c);
                    ++loaded;
                }
            } else if (k == "end") {
                break;
            } else if (k[0] == '#') {
                // comment line
            } else {
                // ignore unknown line
            }
        }

        // Apply seed from replay if allowed and user didn't already set one/load a save
        if (hasSeed && allowSeedOverride && !userProvidedSeedOrSaveAlready) {
            setSeed(replSeed);
            cout << "Replay seed applied: " << replSeed << "\n";
        }

        // Keep the file path (useful for messaging only)
        replayLoaded = true;
        replayPath = path;
        cout << "Loaded " << loaded << " order" << (loaded==1?"":"s")
             << " from replay '" << path << "'.\n";
        if (hasSeed) cout << "Replay metadata: seed=" << replSeed << ", start_hour=" << startHour << "\n";
        return true;
    }

    bool saveToFile(const string& path) const {
        std::ofstream ofs(path, std::ios::out);
        if (!ofs) { cout << "Failed to open '" << path << "' for writing.\n"; return false; }

        // Bump to v2 for battery parameters
        ofs << "MARS_SAVE 2\n";
        ofs << "hour " << s.hour << "\n";
        ofs << "population " << s.population << "\n";
        ofs << "housing " << s.housingCapacity << "\n";
        ofs << "morale " << std::setprecision(17) << s.morale << "\n";
        ofs << std::setprecision(17);
        ofs << "res " << s.res.powerStored << " " << s.res.batteryCapacity << " "
            << s.res.water << " " << s.res.oxygen << " " << s.res.food << " "
            << s.res.metals << " " << s.res.credits << "\n";

        ofs << "buildings " << s.buildings.size() << "\n";
        for (const auto& b : s.buildings) {
            ofs << "b " << static_cast<int>(b.type) << " " << (b.active ? 1 : 0) << "\n";
        }

        ofs << "effects " << s.effects.size() << "\n";
        for (const auto& e : s.effects) {
            ofs << "e " << 0 /* DustStorm */ << " " << e.hoursRemaining << " "
                << e.solarMultiplier << "\n";
        }

        ofs << "lastpower " << s.lastPower.producers << " " << s.lastPower.criticalDemand
            << " " << s.lastPower.nonCriticalDemand << " " << s.lastPower.nonCriticalEff
            << " " << (s.lastPower.blackout ? 1 : 0) << "\n";

        // NEW in v2: persist battery model parameters
        ofs << "battery " << s.batteryCRate << " " << s.batteryEtaIn << " " << s.batteryEtaOut << "\n";

        ofs << "rngseed " << s.rngSeed << "\n";
        std::ostringstream rngoss;
        rngoss << s.rng;
        ofs << "rngstate " << rngoss.str() << "\n";
        ofs << "end\n";
        cout << "Saved to '" << path << "'.\n";
        return true;
    }

    bool loadFromFile(const string& path) {
        std::ifstream ifs(path, std::ios::in);
        if (!ifs) { cout << "Failed to open '" << path << "' for reading.\n"; return false; }

        string tag; int version = 0;
        if (!(ifs >> tag >> version) || tag != "MARS_SAVE" || (version != 1 && version != 2)) {
            cout << "Unrecognized save file header.\n"; return false;
        }

        // Reset minimal state; we will overwrite fields
        GameState loaded;

        string key;
        // Consume endline after header
        ifs.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        while (ifs >> key) {
            if (key == "hour") {
                ifs >> loaded.hour; ifs.ignore(1, '\n');
            } else if (key == "population") {
                ifs >> loaded.population; ifs.ignore(1, '\n');
            } else if (key == "housing") {
                ifs >> loaded.housingCapacity; ifs.ignore(1, '\n');
            } else if (key == "morale") {
                ifs >> loaded.morale; ifs.ignore(1, '\n');
            } else if (key == "res") {
                ifs >> loaded.res.powerStored >> loaded.res.batteryCapacity
                    >> loaded.res.water >> loaded.res.oxygen >> loaded.res.food
                    >> loaded.res.metals >> loaded.res.credits;
                ifs.ignore(1, '\n');
            } else if (key == "buildings") {
                size_t n=0; ifs >> n; ifs.ignore(1, '\n');
                loaded.buildings.clear(); loaded.buildings.reserve(n);
                for (size_t i=0;i<n;++i) {
                    string btag; int t=0, act=1;
                    ifs >> btag >> t >> act; ifs.ignore(1, '\n');
                    if (btag!="b") { cout << "Bad building tag in save.\n"; return false; }
                    BuildingType bt = static_cast<BuildingType>(t);
                    loaded.buildings.push_back(Building{bt, act!=0});
                }
            } else if (key == "effects") {
                size_t n=0; ifs >> n; ifs.ignore(1, '\n');
                loaded.effects.clear(); loaded.effects.reserve(n);
                for (size_t i=0;i<n;++i) {
                    string etag; int t=0, hrs=0; double mult=1.0;
                    ifs >> etag >> t >> hrs >> mult; ifs.ignore(1, '\n');
                    if (etag!="e") { cout << "Bad effect tag in save.\n"; return false; }
                    ActiveEffect e;
                    e.type = EffectType::DustStorm;
                    e.hoursRemaining = hrs;
                    e.solarMultiplier = mult;
                    e.description = "Dust Storm (solar " + std::to_string(int(mult*100)) + "%)";
                    loaded.effects.push_back(e);
                }
            } else if (key == "lastpower") {
                int blackoutInt=0;
                ifs >> loaded.lastPower.producers >> loaded.lastPower.criticalDemand
                    >> loaded.lastPower.nonCriticalDemand >> loaded.lastPower.nonCriticalEff
                    >> blackoutInt;
                loaded.lastPower.blackout = (blackoutInt!=0);
                ifs.ignore(1, '\n');
            } else if (key == "battery") {
                // Present only in v2 saves
                ifs >> loaded.batteryCRate >> loaded.batteryEtaIn >> loaded.batteryEtaOut;
                ifs.ignore(1, '\n');
            } else if (key == "rngseed") {
                unsigned seed=0; ifs >> seed; loaded.rngSeed = seed; ifs.ignore(1, '\n');
            } else if (key == "rngstate") {
                // read rest of line as state
                string rest;
                std::getline(ifs, rest);
                std::istringstream iss(rest);
                iss >> loaded.rng;
            } else if (key == "end") {
                break;
            } else {
                // consume rest of line for unknown key
                ifs.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            }
        }

        // Swap in loaded state
        s = std::move(loaded);
        cout << "Loaded from '" << path << "'.\n";
        return true;
    }

    void runCLI() {
        printWelcome();
        bool running = true;
        while (running) {
            cout << "\n==== Main Menu ====\n"
                 << "1) Show status\n"
                 << "2) Advance 1 hour\n"
                 << "3) Advance 6 hours\n"
                 << "4) Advance 24 hours (1 sol)\n"
                 << "5) Build structure\n"
                 << "6) Tips\n"
                 << "7) Save game\n"
                 << "8) Load game\n"
                 << "9) Power forecast (24h)\n"
                 << "0) Quit\n";
            int choice = readInt("Select: ", 0, 9);
            switch (choice) {
                case 1: showStatus(); break;
                case 2: advanceHours(1); break;
                case 3: advanceHours(6); break;
                case 4: advanceHours(24); break;
                case 5: doBuildMenu(); break;
                case 6: printTips(); break;
                case 7: { string f = readLine("Save file name", "savegame.mc"); saveToFile(f); } break;
                case 8: { string f = readLine("Load file name", "savegame.mc"); loadFromFile(f); } break;
                case 9: forecastHours(24); break;
                case 0: running = false; break;
                default: cout << "Unknown selection.\n"; break;
            }
        }
        cout << "\nGood luck, Commander.\n";
    }

    // Expose a status call for headless/autorun previews
    void showStatus() const { printStatus(); }

    // Expose advancing time for headless/autorun
    void advanceHours(int hours) {
        hours = std::max(0, hours);
        for (int i = 0; i < hours; ++i) {
            // Apply all commands scheduled for THIS hour before events/simulation
            applyCommandsForHour(s.hour);

            maybeSpawnEvents();
            simulateHour();
            tickEffects();

            // Lightweight invariant check each hour (throws if enabled)
            if (!checkInvariants(false) && hardFailOnInvariant) {
                throw std::runtime_error("Simulation invariant failed.");
            }

            ++s.hour;
        }
        cout << "Advanced " << hours << " " << pluralize("hour", hours)
             << ". Now Sol " << sol() << ", Hour " << hourOfSol() << ".\n";
    }

private:
    GameState s;
    bool forecastMode = false; // suppress logs during look-ahead simulations

    // Invariant behavior
    bool hardFailOnInvariant = false;

    // --- Deterministic command queue + (optional) recording ------------------
    std::multimap<long long, Command> pendingCommands;
    string recordPath;
    bool recording = false;
    bool recordHeaderWritten = false;
    bool replayLoaded = false;
    string replayPath;

    // Enqueue a command (and record it if recording is active).
    void submit(const Command& c) {
        pendingCommands.emplace(c.hour, c);
        if (recording) recordCommand(c);
    }

    // Append a single command line to the recording file.
    void recordCommand(const Command& c) {
        if (!recordHeaderWritten || recordPath.empty()) return;
        std::ofstream ofs(recordPath, std::ios::out | std::ios::app);
        if (!ofs) { cout << "Warning: failed to append to '" << recordPath << "'.\n"; return; }
        switch (c.type) {
            case CommandType::Build:
                ofs << "h " << c.hour << " build " << c.a << "\n";
                break;
        }
    }

    // Apply all commands scheduled for the given hour.
    void applyCommandsForHour(long long hourNow) {
        auto range = pendingCommands.equal_range(hourNow);
        for (auto it = range.first; it != range.second; ++it) {
            const Command& c = it->second;
            switch (c.type) {
                case CommandType::Build: {
                    BuildingType bt = static_cast<BuildingType>(c.a);
                    bool ok = tryBuild(bt);
                    if (!forecastMode) {
                        if (ok) cout << "[Order] Build " << to_string(bt) << " completed at start of hour " << hourNow << ".\n";
                        else    cout << "[Order] Build " << to_string(bt) << " FAILED (resources insufficient) at hour " << hourNow << ".\n";
                    }
                } break;
            }
        }
        pendingCommands.erase(range.first, range.second);
    }

    // Queue from UI and apply immediately this hour (so behavior matches old build flow).
    void queueBuildNow(BuildingType t) {
        Command c; c.hour = s.hour; c.type = CommandType::Build; c.a = static_cast<int>(t);
        submit(c);
        // Apply instantly for current hour (keeps UX same as before and ensures recording)
        applyCommandsForHour(s.hour);
    }

    void initStarter() {
        // Starter setup
        addBuilding(BuildingType::Habitat);
        addBuilding(BuildingType::SolarArray);
        addBuilding(BuildingType::SolarArray);
        addBuilding(BuildingType::SolarArray);
        addBuilding(BuildingType::BatteryBank);
        addBuilding(BuildingType::WaterExtractor);
        addBuilding(BuildingType::Greenhouse);
        addBuilding(BuildingType::Electrolyzer);
    }

    // ---- Time/Daylight ------------------------------------------------------

    static constexpr int SOL_HOURS = 24; // simplified sol
    static constexpr int DAYLIGHT_START = 6;
    static constexpr int DAYLIGHT_END   = 18;

    int hourOfSol() const { return static_cast<int>(s.hour % SOL_HOURS); }
    long long sol() const { return s.hour / SOL_HOURS; }

    double daylightFactor() const {
        // Smooth cosine twilight ramp around sunrise/sunset
        constexpr double TW = 1.5; // twilight duration (hours)
        const double h = static_cast<double>(hourOfSol());
        const double a = DAYLIGHT_START - TW; // start of sunrise ramp
        const double b = DAYLIGHT_START + TW; // end of sunrise ramp (full daylight)
        const double c = DAYLIGHT_END   - TW; // start of sunset ramp
        const double d = DAYLIGHT_END   + TW; // end of sunset ramp (night)

        auto ease = [](double t) {
            const double PI = 3.14159265358979323846;
            t = clampv(t, 0.0, 1.0);
            return 0.5 - 0.5 * std::cos(t * PI); // 0..1
        };

        if (h <= a || h >= d) return 0.0;   // night
        if (h >= b && h <= c) return 1.0;   // full daylight
        if (h > a && h < b)   return ease((h - a) / (b - a)); // sunrise ramp
        /* h in (c,d) */      return ease((d - h) / (d - c)); // sunset ramp
    }

    double stormSolarMultiplier() const {
        double mult = 1.0;
        for (const auto& e : s.effects) {
            if (e.type == EffectType::DustStorm) mult *= e.solarMultiplier;
        }
        return mult;
    }

    // ---- Colony mechanics ---------------------------------------------------

    static constexpr double PWR_PER_COLONIST = 0.3;
    static constexpr double WAT_PER_COLONIST = 0.10;
    static constexpr double O2_PER_COLONIST  = 0.50;
    static constexpr double FOOD_PER_COLONIST= 0.05;

    void printWelcome() const {
        cout << "=====================================\n";
        cout << "  MARS COLONY — Starter Simulation\n";
        cout << "=====================================\n";
        cout << "Sol " << sol() << ", Hour " << hourOfSol() << " — Colony initialized.\n";
        cout << "RNG seed: " << s.rngSeed << "\n";
        cout << "Use the menu numbers to choose actions.\n";
    }

    void printStatus() const {
        cout << "\n--- STATUS ---\n";
        cout << "Time: Sol " << sol() << ", Hour " << hourOfSol()
             << (daylightFactor() > 0.0 ? " (daylight)" : " (night)") << "\n";

        cout << std::fixed << std::setprecision(1);
        cout << "Power: " << s.res.powerStored << " / " << s.res.batteryCapacity
             << " | prod " << s.lastPower.producers
             << " | crit " << s.lastPower.criticalDemand
             << " | noncrit " << s.lastPower.nonCriticalDemand
             << " @eff " << (100.0 * s.lastPower.nonCriticalEff) << "%";
        if ( s.lastPower.blackout ) cout << "  [BLACKOUT]";
        cout << "\n";

        // NEW: show battery model params and last-hour battery flows
        cout << "Battery model: C=" << std::setprecision(2) << s.batteryCRate
             << "  eta_in=" << s.batteryEtaIn
             << "  eta_out=" << s.batteryEtaOut
             << "  | last hour: +in " << std::setprecision(1) << s.lastPower.battIn
             << "  -out " << s.lastPower.battOut << " (kWh)\n";

        cout << "Water: "  << s.res.water  << "  "
             << "Oxygen: " << s.res.oxygen << "  "
             << "Food: "   << s.res.food   << "\n";

        cout << "Metals: " << s.res.metals << "  "
             << "Credits: " << s.res.credits << "\n";

        cout << "Population: " << s.population
             << " / Housing: " << s.housingCapacity
             << "  | Morale: " << std::setprecision(2) << s.morale << "\n";

        // Buildings summary
        std::map<string,int> counts;
        for (const auto& b : s.buildings) counts[to_string(b.type)]++;
        cout << "Buildings:\n";
        for (const auto& kv : counts) {
            cout << "  * " << kv.first << " x" << kv.second << "\n";
        }

        // Effects
        if (s.effects.empty()) {
            cout << "Effects: (none)\n";
        } else {
            cout << "Effects:\n";
            for (const auto& e : s.effects) {
                cout << "  * " << e.description << " — " << e.hoursRemaining << "h remaining\n";
            }
        }

        if (recording) {
            cout << "[Recording] Orders are being logged to '" << recordPath << "'.\n";
        }
        if (replayLoaded) {
            cout << "[Replay] Orders have been loaded from '" << replayPath << "'.\n";
        }
    }

    void printTips() const {
        cout << "\n--- TIPS ---\n";
        cout << "* Solar vanishes at night and during dust storms. Battery Banks and RTG help.\n";
        cout << "* Greenhouses boost oxygen/food but use power and water.\n";
        cout << "* Extractor + Electrolyzer: water -> oxygen.\n";
        cout << "* Habitats increase housing; avoid overcrowding for morale.\n";
        cout << "* Watch the power line (prod/crit/noncrit). Avoid blackouts.\n";
        cout << "* Try advancing 6-24 hours, then build with the resources you have.\n";
        cout << "* Save often! You can reload and explore different strategies.\n";
        cout << "* Use --record to capture your build orders; later use --replay to reproduce a run.\n";
    }

    void listBuildOptions() const {
        cout << "\n--- BUILD OPTIONS ---\n";
        printBuildLine(BuildingType::SolarArray);
        printBuildLine(BuildingType::BatteryBank);
        printBuildLine(BuildingType::Habitat);
        printBuildLine(BuildingType::Greenhouse);
        printBuildLine(BuildingType::WaterExtractor);
        printBuildLine(BuildingType::Electrolyzer);
        printBuildLine(BuildingType::RTG);
    }

    void printBuildLine(BuildingType t) const {
        const auto& sp = getSpec(t);
        int idx = static_cast<int>(t);
        cout << (idx+1) << ") " << sp.name
             << "  [Metals " << sp.metalsCost
             << ", Credits " << sp.creditsCost << "]";
        if (sp.housing) cout << "  +" << sp.housing << " housing";
        if (sp.batteryCapacityDelta > 0.0) cout << "  +" << sp.batteryCapacityDelta << " battery cap";
        if (sp.powerProdDay > 0.0) cout << "  (solar +" << sp.powerProdDay << "/h daylight)";
        if (sp.powerProdConst > 0.0) cout << "  (+" << sp.powerProdConst << "/h constant)";
        if (sp.powerCons > 0.0) cout << "  (-" << sp.powerCons << " power/h)";
        cout << "\n";
    }

    void doBuildMenu() {
        listBuildOptions();
        int sel = readInt("Enter number to build (0 to cancel): ", 0, 7);
        if (sel == 0) return;

        BuildingType chosen;
        switch (sel) {
            case 1: chosen = BuildingType::SolarArray; break;
            case 2: chosen = BuildingType::BatteryBank; break;
            case 3: chosen = BuildingType::Habitat; break;
            case 4: chosen = BuildingType::Greenhouse; break;
            case 5: chosen = BuildingType::WaterExtractor; break;
            case 6: chosen = BuildingType::Electrolyzer; break;
            case 7: chosen = BuildingType::RTG; break;
            default: cout << "Invalid selection.\n"; return;
        }

        // Route through deterministic command system (+record if enabled)
        queueBuildNow(chosen);
        // The command executor prints success/failure. Nothing else needed here.
    }

    bool tryBuild(BuildingType t) {
        const auto& sp = getSpec(t);
        if (s.res.metals  < sp.metalsCost)  return false;
        if (s.res.credits < sp.creditsCost) return false;
        s.res.metals  -= sp.metalsCost;
        s.res.credits -= sp.creditsCost;
        addBuilding(t);
        return true;
    }

    void addBuilding(BuildingType t) {
        const auto& sp = getSpec(t);
        s.buildings.push_back(Building{t, true});
        s.housingCapacity += sp.housing;
        s.res.batteryCapacity += sp.batteryCapacityDelta;
        s.res.powerStored = clampv(s.res.powerStored, 0.0, s.res.batteryCapacity);
    }

    // ---- Random Events ------------------------------------------------------

    void maybeSpawnEvents() {
        if (hourOfSol() != 0) return;
        std::uniform_real_distribution<double> U(0.0, 1.0);

        // Dust Storm (18%): 36–96h, solarMultiplier 0.2–0.6
        if (U(s.rng) < 0.18) {
            std::uniform_int_distribution<int> dur(36, 96);
            std::uniform_real_distribution<double> mult(0.2, 0.6);
            ActiveEffect e;
            e.type = EffectType::DustStorm;
            e.hoursRemaining = dur(s.rng);
            e.solarMultiplier = mult(s.rng);
            e.description = "Dust Storm (solar " + std::to_string(int(e.solarMultiplier*100)) + "%)";
            s.effects.push_back(e);
            if (!forecastMode) cout << "[Event] A dust storm rolls in! Solar output reduced.\n";
        }

        // Meteoroid (6%): destroy a random non-battery building
        if (U(s.rng) < 0.06 && !s.buildings.empty()) {
            vector<int> candidates;
            for (int i = 0; i < (int)s.buildings.size(); ++i) {
                auto t = s.buildings[i].type;
                if (t != BuildingType::BatteryBank) candidates.push_back(i);
            }
            if (!candidates.empty()) {
                std::uniform_int_distribution<int> pick(0, (int)candidates.size()-1);
                int idx = candidates[pick(s.rng)];
                auto btype = s.buildings[idx].type;
                if (!forecastMode) cout << "[Event] Meteoroid strike! " << to_string(btype) << " destroyed.\n";
                const auto& sp = getSpec(btype);
                s.housingCapacity -= sp.housing;
                s.housingCapacity = std::max(s.housingCapacity, 0);
                s.buildings.erase(s.buildings.begin() + idx);
                s.morale = clampv(s.morale - 0.08, 0.0, 1.0);
            }
        }

        // Supply Drop (12%)
        if (U(s.rng) < 0.12) {
            s.res.water  += 60.0;
            s.res.oxygen += 120.0;
            s.res.food   += 80.0;
            s.res.metals += 60;
            s.res.credits+= 400;
            if (!forecastMode) cout << "[Event] Orbital supply drop delivered! Stocks replenished.\n";
        }
    }

    void tickEffects() {
        for (auto& e : s.effects) {
            if (e.hoursRemaining > 0) --e.hoursRemaining;
        }
        s.effects.erase(
            std::remove_if(s.effects.begin(), s.effects.end(),
                [&](const ActiveEffect& e){
                    if (e.hoursRemaining <= 0) {
                        if (!forecastMode) cout << "[Weather] " << e.description << " has cleared.\n";
                        return true;
                    }
                    return false;
                }),
            s.effects.end()
        );
    }

    // ---- Non-critical dispatch optimizer (0/1 knapsack) --------------------

    // Choose which non-critical loads to turn ON this hour to maximize utility
    // under a power budget. Utility comes from positive resource flows, weighted
    // by current scarcity (food/oxygen/water).
    std::vector<int> chooseNonCriticalLoads(double powerBudget,
                                            double wFood,
                                            double wO2,
                                            double wWater) const
    {
        struct Item { int idx; int w; double v; }; // w = scaled power, v = utility
        const int scale = 10; // 0.1 power units granularity for DP
        int capacity = (int)(std::max(0.0, powerBudget) * scale + 0.5);

        std::vector<Item> items;
        items.reserve(s.buildings.size());

        for (int i = 0; i < (int)s.buildings.size(); ++i) {
            const auto& b  = s.buildings[i];
            const auto& sp = getSpec(b.type);
            if (!b.active) continue;
            if (!sp.needsPower || sp.isCriticalLoad || sp.powerCons <= 0.0) continue;

            // Utility from positive outputs, biased by scarcity.
            double util = 0.0;
            if (sp.foodFlow   > 0.0) util += wFood  * sp.foodFlow;
            if (sp.oxygenFlow > 0.0) util += wO2    * sp.oxygenFlow;
            if (sp.waterFlow  > 0.0) util += wWater * sp.waterFlow;

            // Soft penalty for very power-hungry loads (efficiency bias).
            util /= (1.0 + 0.05 * sp.powerCons);

            int weight = (int)(sp.powerCons * scale + 0.5);
            if (weight <= 0) continue;

            if (util > 0.0) items.push_back({i, weight, util});
        }

        if (capacity <= 0 || items.empty()) return {};

        const int n = (int)items.size();
        std::vector<std::vector<double>> dp(n + 1, std::vector<double>(capacity + 1, 0.0));
        std::vector<std::vector<char>>   take(n + 1, std::vector<char>(capacity + 1, 0));

        for (int i = 1; i <= n; ++i) {
            int w = items[i - 1].w;
            double v = items[i - 1].v;
            for (int c = 0; c <= capacity; ++c) {
                dp[i][c] = dp[i - 1][c];
                if (w <= c) {
                    double cand = dp[i - 1][c - w] + v;
                    if (cand > dp[i][c]) { dp[i][c] = cand; take[i][c] = 1; }
                }
            }
        }

        // Reconstruct chosen indices
        std::vector<int> chosen;
        for (int i = n, c = capacity; i >= 1; --i) {
            if (take[i][c]) { chosen.push_back(items[i - 1].idx); c -= items[i - 1].w; }
        }
        return chosen;
    }

    // ---- Forecast (what-if) -------------------------------------------------

    void forecastHours(int hours) {
        hours = std::max(0, hours);
        auto backup = s;
        auto cmdBackup = pendingCommands;   // include scheduled orders in look-ahead
        const bool oldFM = forecastMode;
        forecastMode = true;

        vector<double> batt, prod, crit, noncritRun, battIn, battOut;
        vector<int>    solv, hourv;
        vector<char>   blackout;
        batt.reserve(hours); prod.reserve(hours); crit.reserve(hours);
        noncritRun.reserve(hours); battIn.reserve(hours); battOut.reserve(hours);
        solv.reserve(hours); hourv.reserve(hours); blackout.reserve(hours);

        for (int i = 0; i < hours; ++i) {
            // Apply any orders due at this forecast hour (non-destructive; we restore later)
            applyCommandsForHour(s.hour);

            // Do NOT spawn new random events during forecast
            simulateHour();
            tickEffects();
            ++s.hour;

            batt.push_back(s.res.powerStored);
            prod.push_back(s.lastPower.producers);
            crit.push_back(s.lastPower.criticalDemand);
            noncritRun.push_back(s.lastPower.nonCriticalDemand * s.lastPower.nonCriticalEff);
            battIn.push_back(s.lastPower.battIn);
            battOut.push_back(s.lastPower.battOut);
            blackout.push_back(s.lastPower.blackout ? 1 : 0);
            solv.push_back(static_cast<int>(s.hour / SOL_HOURS));
            hourv.push_back(static_cast<int>(s.hour % SOL_HOURS));
        }

        // Restore state + command queue
        forecastMode = oldFM;
        s = std::move(backup);
        pendingCommands = std::move(cmdBackup);

        // Summaries
        double minBat = *std::min_element(batt.begin(), batt.end());
        double maxBat = *std::max_element(batt.begin(), batt.end());
        int firstBO = -1;
        for (int i = 0; i < hours; ++i) if (blackout[i]) { firstBO = i; break; }
        double sumIn  = std::accumulate(battIn.begin(),  battIn.end(),  0.0);
        double sumOut = std::accumulate(battOut.begin(), battOut.end(), 0.0);

        cout << "\n=== Power Forecast (" << hours << "h) ===\n";
        cout << "Battery range: " << std::fixed << std::setprecision(1)
             << minBat << " .. " << maxBat << "  (cap " << s.res.batteryCapacity << ")\n";
        if (firstBO >= 0) {
            cout << "BLACKOUT predicted at +" << firstBO
                 << "h (Sol " << solv[firstBO] << ", Hour " << hourv[firstBO] << ")\n";
        } else {
            cout << "No blackout predicted.\n";
        }
        cout << "Charge scheduled: " << std::setprecision(1) << sumIn
             << " kWh, Discharge scheduled: " << sumOut << " kWh\n";

        cout << "\nhr  sol:hr  prod  crit  noncrit  batt  note\n";
        for (int i = 0; i < hours; i += 6) {
            cout << std::setw(2) << i
                 << "  " << std::setw(2) << solv[i] << ":" << std::setw(2) << hourv[i]
                 << "  " << std::setw(5) << std::fixed << std::setprecision(1) << prod[i]
                 << "  " << std::setw(5) << crit[i]
                 << "  " << std::setw(7) << noncritRun[i]
                 << "  " << std::setw(6) << batt[i]
                 << (blackout[i] ? "  *BLACKOUT*" : "")
                 << "\n";
        }
    }

    // ---- Power & resource update -------------------------------------------

    void simulateHour() {
        // 1) Power production
        double producers = 0.0;
        double daylight = daylightFactor();
        double stormMult = stormSolarMultiplier();

        for (const auto& b : s.buildings) {
            if (!b.active) continue;
            const auto& sp = getSpec(b.type);
            if (sp.powerProdConst > 0.0) producers += sp.powerProdConst;
            if (sp.powerProdDay   > 0.0) producers += sp.powerProdDay * daylight * stormMult;
        }

        // 2) Consumption — aggregate critical vs non-critical potential demand
        double critical = s.population * PWR_PER_COLONIST;
        double noncritPotential = 0.0;

        for (const auto& b : s.buildings) {
            if (!b.active) continue;
            const auto& sp = getSpec(b.type);
            if (sp.powerCons <= 0.0 || !sp.needsPower) continue;
            if (sp.isCriticalLoad) critical += sp.powerCons;
            else                   noncritPotential  += sp.powerCons;
        }

        // 3) Shortage-aware weights (smaller hours-of-supply => larger weight).
        auto hoursOf = [&](double store, double ratePerHour){
            if (ratePerHour <= 0.0) return 9999.0;
            return (store / ratePerHour);
        };
        double hFood_before  = hoursOf(s.res.food,   s.population * FOOD_PER_COLONIST);
        double hWater_before = hoursOf(s.res.water,  s.population * WAT_PER_COLONIST);
        double hO2_before    = hoursOf(s.res.oxygen, s.population * O2_PER_COLONIST);

        auto weightFromHours = [](double h) {
            // 1 + 72/(h+1): ~73 when ~0 hours left; ~2 when ~35h; ~1 when 72h+
            return 1.0 + 72.0 / (h + 1.0);
        };
        double wFood  = weightFromHours(hFood_before);
        double wWater = weightFromHours(hWater_before);
        double wO2    = weightFromHours(hO2_before);

        // 4) Determine budget for non-critical loads, respecting battery C-rate
        const double cap    = s.res.batteryCapacity;
        const double soc0   = s.res.powerStored;
        const double cRate  = s.batteryCRate;
        const double etaIn  = s.batteryEtaIn;
        const double etaOut = s.batteryEtaOut;

        const double deliverByCRate = cap * cRate;      // kWh this hour
        const double deliverBySoC   = soc0 * etaOut;    // kWh this hour
        const double deliverableMax = std::max(0.0, std::min(deliverByCRate, deliverBySoC));

        const double surplusAfterCritical = std::max(0.0, producers - critical);
        const double deficitAfterCritical = std::max(0.0, critical - producers);

        const double reservedForCritical = std::min(deficitAfterCritical, deliverableMax);
        const double remainingDeliverable = std::max(0.0, deliverableMax - reservedForCritical);

        const double nonCritBudget = surplusAfterCritical + remainingDeliverable;

        // 5) Choose which non-critical loads to run under that budget
        std::vector<char> runFlags(s.buildings.size(), 0);
        double noncritUsed = 0.0;
        if (nonCritBudget > 0.0) {
            auto chosen = chooseNonCriticalLoads(nonCritBudget, wFood, wO2, wWater);
            for (int idx : chosen) {
                runFlags[idx] = 1;
                noncritUsed += getSpec(s.buildings[idx].type).powerCons;
            }
        }

        // 6) Battery dispatch with C-rate limits and round-trip efficiency
        // Net after selected loads (positive -> surplus to charge; negative -> need discharge)
        double netAfterLoads = producers - critical - noncritUsed;

        // Reset last-hour battery telemetry
        s.lastPower.battIn  = 0.0;
        s.lastPower.battOut = 0.0;
        s.lastPower.chargeCRateLimited    = false;
        s.lastPower.dischargeCRateLimited = false;

        double soc = soc0;
        const double chargePowerLimit    = cap * cRate;
        const double dischargePowerLimit = cap * cRate;

        if (netAfterLoads > 1e-9) {
            // Surplus -> charge (limited by C-rate and free space / etaIn)
            const double byCRate = chargePowerLimit;
            const double byRoom  = (cap - soc) / std::max(1e-12, etaIn);
            const double canInput = std::max(0.0, std::min({ netAfterLoads, byCRate, byRoom }));
            if (canInput < netAfterLoads - 1e-9 && canInput < byCRate - 1e-9) {
                s.lastPower.chargeCRateLimited = true;
            }
            soc += canInput * etaIn;     // store after charge losses
            s.lastPower.battIn = canInput;   // producers -> battery
            netAfterLoads -= canInput;   // remainder is curtailed (unused)
        } else if (netAfterLoads < -1e-9) {
            // Deficit -> discharge (limited by C-rate and SoC * etaOut)
            double deficit = -netAfterLoads;
            const double deliverByCR = dischargePowerLimit;
            const double deliverBySo = soc * etaOut;
            const double delivered   = std::max(0.0, std::min({ deficit, deliverByCR, deliverBySo }));
            if (delivered < deficit - 1e-9 && delivered < deliverByCR - 1e-9) {
                s.lastPower.dischargeCRateLimited = true;
            }
            soc -= delivered / std::max(1e-12, etaOut); // remove pre-efficiency energy from SoC
            s.lastPower.battOut = delivered;            // energy delivered to bus
            netAfterLoads += delivered;
        }

        s.res.powerStored = clampv(soc, 0.0, cap);
        bool blackout = (netAfterLoads < -1e-6); // still negative after max discharge

        // 7) Resource flows from buildings (gated by power and dispatch)
        double waterDelta  = 0.0;
        double oxygenDelta = 0.0;
        double foodDelta   = 0.0;

        for (int i = 0; i < (int)s.buildings.size(); ++i) {
            const auto& b  = s.buildings[i];
            const auto& sp = getSpec(b.type);
            if (!b.active) continue;
            if (sp.waterFlow == 0.0 && sp.oxygenFlow == 0.0 && sp.foodFlow == 0.0) continue;

            double eff = 1.0;
            if (sp.needsPower) {
                if (sp.isCriticalLoad) eff = blackout ? 0.0 : 1.0;
                else                   eff = (!blackout && runFlags[i]) ? 1.0 : 0.0; // discrete ON/OFF + blackout gating
            }

            waterDelta  += sp.waterFlow  * eff;
            oxygenDelta += sp.oxygenFlow * eff;
            foodDelta   += sp.foodFlow   * eff;
        }

        // 8) Population consumption
        waterDelta  -= s.population * WAT_PER_COLONIST;
        oxygenDelta -= s.population * O2_PER_COLONIST;
        foodDelta   -= s.population * FOOD_PER_COLONIST;

        // 9) Apply
        s.res.water  = std::max(0.0, s.res.water  + waterDelta);
        s.res.oxygen = std::max(0.0, s.res.oxygen + oxygenDelta);
        s.res.food   = std::max(0.0, s.res.food   + foodDelta);

        // 10) Morale
        double moraleDelta = 0.0;
        double hFood  = hoursOf(s.res.food,   s.population * FOOD_PER_COLONIST);
        double hWater = hoursOf(s.res.water,  s.population * WAT_PER_COLONIST);
        double hO2    = hoursOf(s.res.oxygen, s.population * O2_PER_COLONIST);

        if (blackout)         moraleDelta -= 0.04;
        if (hFood  < 24.0)    moraleDelta -= 0.02;
        if (hWater < 24.0)    moraleDelta -= 0.02;
        if (hO2    < 24.0)    moraleDelta -= 0.03;

        if (!blackout && hFood > 72 && hWater > 72 && hO2 > 72 && s.res.powerStored > s.res.batteryCapacity * 0.5) {
            moraleDelta += 0.01;
        }
        if (s.population > s.housingCapacity) moraleDelta -= 0.02;
        s.morale = clampv(s.morale + moraleDelta, 0.0, 1.0);

        // 11) Power report
        s.lastPower.producers = producers;
        s.lastPower.criticalDemand = critical;
        s.lastPower.nonCriticalDemand = noncritPotential;
        // If there was a blackout, treat non-critical as not effectively served
        s.lastPower.nonCriticalEff = (noncritPotential > 0.0)
            ? ( (blackout ? 0.0 : (noncritUsed / noncritPotential)) )
            : 0.0;
        s.lastPower.blackout = blackout;

        // 12) Warnings
        if (!forecastMode && (s.res.oxygen <= 0.0 || s.res.food <= 0.0 || s.res.water <= 0.0)) {
            cout << "[Warning] Critical shortage: ";
            if (s.res.oxygen <= 0.0) cout << "Oxygen ";
            if (s.res.water  <= 0.0) cout << "Water ";
            if (s.res.food   <= 0.0) cout << "Food ";
            cout << "!\n";
        }
    }

    // ---- Invariants ---------------------------------------------------------

    bool checkInvariants(bool verbose) const {
        auto finite = [](double v){ return std::isfinite(v); };
        bool ok = true;
        auto bad = [&](const char* msg){
            if (verbose) cout << "[Invariant] " << msg << "\n";
            ok = false;
        };

        const auto& r = s.res;
        if (!finite(r.powerStored) || r.powerStored < -1e-9) bad("powerStored finite & >= 0");
        if (!finite(r.batteryCapacity) || r.batteryCapacity < -1e-9) bad("batteryCapacity finite & >= 0");
        if (r.powerStored > r.batteryCapacity + 1e-6) bad("powerStored <= batteryCapacity");
        if (!finite(r.water)  || r.water  < -1e-9) bad("water >= 0");
        if (!finite(r.oxygen) || r.oxygen < -1e-9) bad("oxygen >= 0");
        if (!finite(r.food)   || r.food   < -1e-9) bad("food >= 0");

        if (s.population < 0) bad("population >= 0");
        if (s.housingCapacity < 0) bad("housingCapacity >= 0");

        if (!finite(s.morale) || s.morale < -1e-9 || s.morale > 1.0 + 1e-9) bad("morale in [0,1]");
        if (!finite(s.batteryCRate) || s.batteryCRate < 0) bad("batteryCRate >= 0");
        if (!finite(s.batteryEtaIn) || s.batteryEtaIn <= 0 || s.batteryEtaIn > 1.0) bad("batteryEtaIn in (0,1]");
        if (!finite(s.batteryEtaOut)|| s.batteryEtaOut<= 0 || s.batteryEtaOut> 1.0) bad("batteryEtaOut in (0,1]");

        const auto& lp = s.lastPower;
        if (!finite(lp.producers) || !finite(lp.criticalDemand) || !finite(lp.nonCriticalDemand) || !finite(lp.nonCriticalEff))
            bad("lastPower fields finite");
        if (lp.nonCriticalEff < -1e-6 || lp.nonCriticalEff > 1.0 + 1e-6) bad("nonCriticalEff in [0,1]");
        if (!finite(lp.battIn)  || lp.battIn  < -1e-9) bad("battIn >= 0");
        if (!finite(lp.battOut) || lp.battOut < -1e-9) bad("battOut >= 0");

        if (s.hour < 0) bad("hour >= 0");

        return ok;
    }
};

// ----------- Entry point -----------------------------------------------------

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    bool noPause = false;
    long long autorunHours = 0;
    bool headless = false;

    bool seedProvided = false;
    uint32_t seedOverride = 0;
    string loadPath, savePath;

    // NEW: recording/replay paths
    string recordPath, replayPath;

    // NEW: invariant & self-test flags
    bool checkInvariantsFlag = false;
    bool runSelfTestFlag = false;

    // Parse simple CLI flags
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--autorun" && i + 1 < argc) {
            autorunHours = std::stoll(argv[++i]);
        } else if (arg == "--headless" && i + 1 < argc) {
            autorunHours = std::stoll(argv[++i]);
            headless = true;
        } else if (arg == "--no-pause") {
            noPause = true;
        } else if (arg == "--seed" && i + 1 < argc) {
            seedOverride = static_cast<uint32_t>(std::stoul(argv[++i]));
            seedProvided = true;
        } else if (arg == "--load" && i + 1 < argc) {
            loadPath = argv[++i];
        } else if (arg == "--save" && i + 1 < argc) {
            savePath = argv[++i];
        } else if (arg == "--record" && i + 1 < argc) {
            recordPath = argv[++i];
        } else if (arg == "--replay" && i + 1 < argc) {
            replayPath = argv[++i];
        } else if (arg == "--check-invariants") {
            checkInvariantsFlag = true;
        } else if (arg == "--selftest") {
            runSelfTestFlag = true;
        }
    }

    try {
        Game g;

        // Early self-test path (for CI): do nothing else.
        if (runSelfTestFlag) {
            int code = g.runSelfTest();
            return code;
        }

        // If user provided a seed explicitly, apply it first.
        if (seedProvided) g.setSeed(seedOverride);

        // If we loaded a save, that sets RNG state deterministically; do it before replay.
        bool saveLoaded = false;
        if (!loadPath.empty()) {
            saveLoaded = g.loadFromFile(loadPath);
        }

        // Start recording (writes header with current seed + hour)
        if (!recordPath.empty()) {
            g.startRecordingTo(recordPath);
        }

        // Load replay commands (may also set seed if allowed and no save/seed already)
        if (!replayPath.empty()) {
            const bool allowSeedOverride = true;
            const bool userProvidedSeedOrSave = seedProvided || saveLoaded;
            g.loadReplayFile(replayPath, allowSeedOverride, userProvidedSeedOrSave);
        }

        // Enable hard invariant checking if requested.
        if (checkInvariantsFlag) g.enableHardInvariants(true);

        if (autorunHours > 0) {
            g.advanceHours((int)autorunHours);
            g.showStatus();
            if (!savePath.empty()) {
                g.saveToFile(savePath);
            }
            if (headless) {
#ifdef _WIN32
                if (!noPause) {
                    cout << "\n(Headless) Press Enter to exit...";
                    cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                    cin.get();
                }
#endif
                return 0;
            }
        }

        g.runCLI();

        if (!savePath.empty()) {
            g.saveToFile(savePath);
        }

#ifdef _WIN32
        if (!noPause) {
            cout << "\nPress Enter to exit...";
            cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            cin.get();
        }
#endif
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nFatal error: " << e.what() << "\n";
#ifdef _WIN32
        if (!noPause) {
            std::cerr << "Press Enter to close...";
            cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            cin.get();
        }
#endif
        return 1;
    } catch (...) {
        std::cerr << "\nUnknown fatal error.\n";
#ifdef _WIN32
        if (!noPause) {
            std::cerr << "Press Enter to close...";
            cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            cin.get();
        }
#endif
        return 2;
    }
}
