/*
  Mars Colony — Windows-friendly starter simulation (C++17, single-file)

  Key improvements for Windows:
   • Robust menu input (line-based), no silent exit on bad input.
   • Optional CLI flags: --autorun N, --headless N, --no-pause, --seed U32, --load FILE, --save FILE.
   • Try/catch with clear error messages instead of sudden close.
   • Text simplified to ASCII for maximum console compatibility.
   • Deterministic save/load (versioned text format) + reproducible RNG seeds.
   • Discrete non-critical power dispatch via a tiny 0/1 knapsack optimizer.

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
    double nonCriticalEff = 0.0; // 0..1 (share of non-critical demand actually run)
    bool blackout = false;
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
    }
};

class Game {
public:
    Game() { initStarter(); }

    void setSeed(uint32_t seed) {
        s.rngSeed = seed;
        s.rng.seed(seed);
    }

    bool saveToFile(const string& path) const {
        std::ofstream ofs(path, std::ios::out);
        if (!ofs) { cout << "Failed to open '" << path << "' for writing.\n"; return false; }

        ofs << "MARS_SAVE 1\n";
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
        if (!(ifs >> tag >> version) || tag != "MARS_SAVE" || version != 1) {
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
            maybeSpawnEvents();
            simulateHour();
            tickEffects();
            ++s.hour;
        }
        cout << "Advanced " << hours << " " << pluralize("hour", hours)
             << ". Now Sol " << sol() << ", Hour " << hourOfSol() << ".\n";
    }

private:
    GameState s;
    bool forecastMode = false; // suppress logs during look-ahead simulations

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

        if (tryBuild(chosen)) {
            cout << "Construction complete: " << to_string(chosen) << "\n";
        } else {
            cout << "Not enough resources to build that.\n";
        }
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
        const bool oldFM = forecastMode;
        forecastMode = true;

        vector<double> batt, prod, crit, noncritRun;
        vector<int>    solv, hourv;
        vector<char>   blackout;
        batt.reserve(hours); prod.reserve(hours); crit.reserve(hours);
        noncritRun.reserve(hours); solv.reserve(hours); hourv.reserve(hours); blackout.reserve(hours);

        for (int i = 0; i < hours; ++i) {
            // Do NOT spawn new random events during forecast
            simulateHour();
            tickEffects();
            ++s.hour;

            batt.push_back(s.res.powerStored);
            prod.push_back(s.lastPower.producers);
            crit.push_back(s.lastPower.criticalDemand);
            noncritRun.push_back(s.lastPower.nonCriticalDemand * s.lastPower.nonCriticalEff);
            blackout.push_back(s.lastPower.blackout ? 1 : 0);
            solv.push_back(static_cast<int>(s.hour / SOL_HOURS));
            hourv.push_back(static_cast<int>(s.hour % SOL_HOURS));
        }

        // Restore state
        forecastMode = oldFM;
        s = std::move(backup);

        // Summaries
        double minBat = *std::min_element(batt.begin(), batt.end());
        double maxBat = *std::max_element(batt.begin(), batt.end());
        int firstBO = -1;
        for (int i = 0; i < hours; ++i) if (blackout[i]) { firstBO = i; break; }

        cout << "\n=== Power Forecast (" << hours << "h) ===\n";
        cout << "Battery range: " << std::fixed << std::setprecision(1)
             << minBat << " .. " << maxBat << "  (cap " << s.res.batteryCapacity << ")\n";
        if (firstBO >= 0) {
            cout << "BLACKOUT predicted at +" << firstBO
                 << "h (Sol " << solv[firstBO] << ", Hour " << hourv[firstBO] << ")\n";
        } else {
            cout << "No blackout predicted.\n";
        }

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

        // 2) Consumption
        double critical = s.population * PWR_PER_COLONIST;
        double noncrit = 0.0;

        for (const auto& b : s.buildings) {
            if (!b.active) continue;
            const auto& sp = getSpec(b.type);
            if (sp.powerCons <= 0.0 || !sp.needsPower) continue;
            if (sp.isCriticalLoad) critical += sp.powerCons;
            else                   noncrit  += sp.powerCons;
        }

        // Shortage-aware weights (smaller hours-of-supply => larger weight).
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

        // 3) Allocate power/battery using discrete ON/OFF dispatch for non-critical loads
        double availableForNoncrit = s.res.powerStored + producers - critical;
        bool blackout = (availableForNoncrit < 0.0);

        std::vector<char> runFlags(s.buildings.size(), 0);
        double noncritUsed = 0.0;

        if (!blackout) {
            auto chosen = chooseNonCriticalLoads(availableForNoncrit, wFood, wO2, wWater);
            for (int idx : chosen) {
                runFlags[idx] = 1;
                noncritUsed += getSpec(s.buildings[idx].type).powerCons;
            }
        }

        // Compute battery change from chosen dispatch (no negative battery, but record blackout if critical unmet)
        double used = critical + noncritUsed;
        s.res.powerStored += (producers - used);
        s.res.powerStored = clampv(s.res.powerStored, 0.0, s.res.batteryCapacity);

        // 4) Resource flows from buildings (gated by power and dispatch)
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
                else                   eff = runFlags[i] ? 1.0 : 0.0; // discrete ON/OFF
            }

            waterDelta  += sp.waterFlow  * eff;
            oxygenDelta += sp.oxygenFlow * eff;
            foodDelta   += sp.foodFlow   * eff;
        }

        // 5) Population consumption
        waterDelta  -= s.population * WAT_PER_COLONIST;
        oxygenDelta -= s.population * O2_PER_COLONIST;
        foodDelta   -= s.population * FOOD_PER_COLONIST;

        // 6) Apply
        s.res.water  = std::max(0.0, s.res.water  + waterDelta);
        s.res.oxygen = std::max(0.0, s.res.oxygen + oxygenDelta);
        s.res.food   = std::max(0.0, s.res.food   + foodDelta);

        // 7) Morale
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

        // 8) Power report
        s.lastPower.producers = producers;
        s.lastPower.criticalDemand = critical;
        s.lastPower.nonCriticalDemand = noncrit;
        s.lastPower.nonCriticalEff = (noncrit > 0.0) ? (noncritUsed / noncrit) : 0.0; // share of non-crit demand we ran
        s.lastPower.blackout = blackout;

        // 9) Warnings
        if (!forecastMode && (s.res.oxygen <= 0.0 || s.res.food <= 0.0 || s.res.water <= 0.0)) {
            cout << "[Warning] Critical shortage: ";
            if (s.res.oxygen <= 0.0) cout << "Oxygen ";
            if (s.res.water  <= 0.0) cout << "Water ";
            if (s.res.food   <= 0.0) cout << "Food ";
            cout << "!\n";
        }
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
        }
    }

    try {
        Game g;
        if (seedProvided) g.setSeed(seedOverride);

        if (!loadPath.empty()) {
            g.loadFromFile(loadPath);
        }

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
