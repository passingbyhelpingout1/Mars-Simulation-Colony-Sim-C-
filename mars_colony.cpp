/*
  Mars Colony — starter simulation (single-file, C++17)

  What you get:
   • Time-stepped simulation (hourly ticks; "sols" are 24 hrs for simplicity).
   • Core resources: power storage (battery), water, oxygen, food, metals, credits.
   • Population & morale; housing capacity via Habitats.
   • Buildings with costs and hourly effects (Solar Arrays, Battery Banks, Habitat, Greenhouse, Water Extractor, Electrolyzer, RTG).
   • Basic power model: producers (solar, RTG) vs critical & non-critical loads, battery charge/discharge.
   • Random events: Dust storms (reduce solar), Meteoroids (damage a random building), Supply drops (grant resources).
   • Text UI: show status, advance time, build structures, and tips.
   • Clear TODO hooks to expand mechanics (research, tech tree, map tiles, pathfinding, maintenance, etc).

  Design choices kept intentionally simple and readable for a starting project.
  Extend freely: add files later (headers/implementation), or keep it single-file during prototyping.
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
    double powerProdDay   = 0.0; // used by Solar Arrays (multiplied by daylight & storms)
    double powerProdConst = 0.0; // used by RTG (constant day & night)
    double powerCons      = 0.0; // consumption when active

    // Resource flows (per hour, positive = production, negative = consumption)
    double waterFlow  = 0.0;
    double oxygenFlow = 0.0;
    double foodFlow   = 0.0;

    // Other effects
    int housing = 0;                  // added housing capacity (Habitat)
    double batteryCapacityDelta = 0;  // added battery capacity (Battery Bank)

    // Build costs
    int metalsCost   = 0;
    int creditsCost  = 0;

    bool needsPower       = false; // building requires power to operate
    bool isCriticalLoad   = false; // included in critical baseline (kept on if at all possible)
};

struct Building {
    BuildingType type;
    bool active = true; // starter toggle; you could add manual on/off later
};

enum class EffectType { DustStorm };

struct ActiveEffect {
    EffectType type;
    int hoursRemaining = 0;
    // For DustStorm
    double solarMultiplier = 1.0;
    string description;
};

// Aggregate resources held by the colony
struct ColonyResources {
    // Stored electrical energy (arbitrary units), and max capacity
    double powerStored = 300.0;
    double batteryCapacity = 600.0;

    // Consumable stores
    double water  = 100.0;
    double oxygen = 200.0;
    double food   = 100.0;

    // Currencies / materials
    int metals  = 200;
    int credits = 1000;
};

struct LastPowerReport {
    double producers = 0.0;
    double criticalDemand = 0.0;
    double nonCriticalDemand = 0.0;
    double nonCriticalEff = 0.0; // 0..1 how much of non-critical ran
    bool blackout = false;       // unmet critical load occurred
};

// ----------- Specs database --------------------------------------------------

static const BuildingSpec& getSpec(BuildingType t) {
    // NOTE: Values are intentionally simple/arcade; tune for your design goals.
    static const std::map<BuildingType, BuildingSpec> DB = {
        { BuildingType::SolarArray,
          BuildingSpec{
              "Solar Array",
              /*powerProdDay*/   25.0,
              /*powerProdConst*/ 0.0,
              /*powerCons*/       0.0,
              /*waterFlow*/       0.0,
              /*oxygenFlow*/      0.0,
              /*foodFlow*/        0.0,
              /*housing*/         0,
              /*batteryCapΔ*/     0.0,
              /*metals*/         50,
              /*credits*/       100,
              /*needsPower*/   false,
              /*isCritical*/   false
          }
        },
        { BuildingType::BatteryBank,
          BuildingSpec{
              "Battery Bank",
              /*powerProdDay*/   0.0,
              /*powerProdConst*/ 0.0,
              /*powerCons*/      0.0,
              /*waterFlow*/      0.0,
              /*oxygenFlow*/     0.0,
              /*foodFlow*/       0.0,
              /*housing*/        0,
              /*batteryCapΔ*/    200.0,
              /*metals*/         40,
              /*credits*/        50,
              /*needsPower*/   false,
              /*isCritical*/   false
          }
        },
        { BuildingType::Habitat,
          BuildingSpec{
              "Habitat",
              /*powerProdDay*/   0.0,
              /*powerProdConst*/ 0.0,
              /*powerCons*/      2.0,  // keep lights/air handlers running
              /*waterFlow*/      0.0,
              /*oxygenFlow*/     0.0,
              /*foodFlow*/       0.0,
              /*housing*/        5,
              /*batteryCapΔ*/    0.0,
              /*metals*/        100,
              /*credits*/       500,
              /*needsPower*/   true,
              /*isCritical*/   true
          }
        },
        { BuildingType::Greenhouse,
          BuildingSpec{
              "Greenhouse",
              /*powerProdDay*/   0.0,
              /*powerProdConst*/ 0.0,
              /*powerCons*/     12.0,
              /*waterFlow*/     -2.0,   // consumes water
              /*oxygenFlow*/     1.0,   // produces oxygen
              /*foodFlow*/       2.0,   // produces food
              /*housing*/        0,
              /*batteryCapΔ*/    0.0,
              /*metals*/         80,
              /*credits*/       400,
              /*needsPower*/   true,
              /*isCritical*/   false
          }
        },
        { BuildingType::WaterExtractor,
          BuildingSpec{
              "Water Extractor",
              /*powerProdDay*/   0.0,
              /*powerProdConst*/ 0.0,
              /*powerCons*/      8.0,
              /*waterFlow*/      3.0,  // produces water
              /*oxygenFlow*/     0.0,
              /*foodFlow*/       0.0,
              /*housing*/        0,
              /*batteryCapΔ*/    0.0,
              /*metals*/         60,
              /*credits*/       300,
              /*needsPower*/   true,
              /*isCritical*/   false
          }
        },
        { BuildingType::Electrolyzer,
          BuildingSpec{
              "Electrolyzer",
              /*powerProdDay*/   0.0,
              /*powerProdConst*/ 0.0,
              /*powerCons*/     10.0,
              /*waterFlow*/     -1.0,  // consumes water
              /*oxygenFlow*/     1.5,  // produces oxygen
              /*foodFlow*/       0.0,
              /*housing*/        0,
              /*batteryCapΔ*/    0.0,
              /*metals*/         50,
              /*credits*/       350,
              /*needsPower*/   true,
              /*isCritical*/   false
          }
        },
        { BuildingType::RTG,
          BuildingSpec{
              "RTG",
              /*powerProdDay*/   0.0,
              /*powerProdConst*/ 30.0, // constant trickle power
              /*powerCons*/      0.0,
              /*waterFlow*/      0.0,
              /*oxygenFlow*/     0.0,
              /*foodFlow*/       0.0,
              /*housing*/        0,
              /*batteryCapΔ*/    0.0,
              /*metals*/        200,
              /*credits*/      2000,
              /*needsPower*/   false,
              /*isCritical*/   false
          }
        },
    };
    return DB.at(t);
}

// ----------- Simulation ------------------------------------------------------

struct GameState {
    long long hour = 0;     // total hours elapsed
    int population = 5;
    int housingCapacity = 5;
    double morale = 0.75;   // 0..1

    ColonyResources res;
    vector<Building> buildings;
    vector<ActiveEffect> effects;
    LastPowerReport lastPower;

    std::mt19937 rng{ std::random_device{}() };
};

class Game {
public:
    Game() {
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
                 << "0) Quit\n"
                 << "Select: ";
            int choice = -1;
            if (!(cin >> choice)) break;

            switch (choice) {
                case 1: printStatus(); break;
                case 2: advanceHours(1); break;
                case 3: advanceHours(6); break;
                case 4: advanceHours(24); break;
                case 5: doBuildMenu(); break;
                case 6: printTips(); break;
                case 0: running = false; break;
                default: cout << "Unknown selection.\n"; break;
            }
        }
        cout << "\nGood luck, Commander. o7\n";
    }

private:
    GameState s;

    // ---- Time/Daylight ------------------------------------------------------

    static constexpr int SOL_HOURS = 24; // simplification for starter
    static constexpr int DAYLIGHT_START = 6;
    static constexpr int DAYLIGHT_END   = 18; // [6,18) => 12 hours daylight

    int hourOfSol() const { return static_cast<int>(s.hour % SOL_HOURS); }
    long long sol() const { return s.hour / SOL_HOURS; }

    double daylightFactor() const {
        int h = hourOfSol();
        return (h >= DAYLIGHT_START && h < DAYLIGHT_END) ? 1.0 : 0.0;
    }

    double stormSolarMultiplier() const {
        double mult = 1.0;
        for (const auto& e : s.effects) {
            if (e.type == EffectType::DustStorm) mult *= e.solarMultiplier;
        }
        return mult;
    }

    // ---- Colony mechanics ---------------------------------------------------

    // Per-colonist hourly consumption (very arcade numbers; tweak!)
    static constexpr double PWR_PER_COLONIST = 0.3; // critical life support
    static constexpr double WAT_PER_COLONIST = 0.10;
    static constexpr double O2_PER_COLONIST  = 0.50;
    static constexpr double FOOD_PER_COLONIST= 0.05;

    void printWelcome() const {
        cout << "=====================================\n";
        cout << "  MARS COLONY — Starter Simulation\n";
        cout << "=====================================\n";
        cout << "Sol " << sol() << ", Hour " << hourOfSol() << " — Colony initialized.\n";
        cout << "Type numbers to choose actions. Build things, keep people alive.\n";
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
        for (const auto& [name, n] : counts) {
            cout << "  • " << name << " x" << n << "\n";
        }

        // Effects
        if (s.effects.empty()) {
            cout << "Effects: (none)\n";
        } else {
            cout << "Effects:\n";
            for (const auto& e : s.effects) {
                cout << "  • " << e.description << " — " << e.hoursRemaining << "h remaining\n";
            }
        }
    }

    void printTips() const {
        cout << "\n--- TIPS ---\n";
        cout << "• Solar power vanishes at night and during dust storms — build Battery Banks and consider an RTG.\n";
        cout << "• Greenhouses help with oxygen and food but draw lots of power and water.\n";
        cout << "• Water Extractors and Electrolyzers complement each other (H2O -> O2).\n";
        cout << "• Habitats increase housing; keep population <= housing for good morale.\n";
        cout << "• Watch the hourly power report (prod/crit/noncrit). Avoid blackouts to keep morale up.\n";
        cout << "• Try advancing a few hours, then build with the resources you have.\n";
        cout << "TODO ideas: tech tree, repair/maintenance, map tiles, trading, research, hazards, UI.\n";
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
        cout << "Enter number to build (0 to cancel): ";
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
        int sel = 0;
        if (!(cin >> sel)) return;
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
        // Clamp stored power to new capacity (if capacity decreased in future designs)
        s.res.powerStored = clampv(s.res.powerStored, 0.0, s.res.batteryCapacity);
    }

    // ---- Random Events ------------------------------------------------------

    void maybeSpawnEvents() {
        // Trigger chance once per sol at hour 0
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
            cout << "[Event] A dust storm rolls in! Solar output reduced.\n";
        }

        // Meteoroid (6%): damage a random non-battery building
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
                cout << "[Event] Meteoroid strike! " << to_string(btype) << " destroyed.\n";
                // If habitat destroyed and reduces capacity, morale hit
                const auto& sp = getSpec(btype);
                s.housingCapacity -= sp.housing;
                s.housingCapacity = std::max(s.housingCapacity, 0);
                s.buildings.erase(s.buildings.begin() + idx);
                s.morale = clampv(s.morale - 0.08, 0.0, 1.0);
            }
        }

        // Supply Drop (12%): grants resources
        if (U(s.rng) < 0.12) {
            s.res.water  += 60.0;
            s.res.oxygen += 120.0;
            s.res.food   += 80.0;
            s.res.metals += 60;
            s.res.credits+= 400;
            cout << "[Event] Orbital supply drop delivered! Stocks replenished.\n";
        }
    }

    void tickEffects() {
        for (auto& e : s.effects) {
            if (e.hoursRemaining > 0) --e.hoursRemaining;
        }
        // Remove expired
        s.effects.erase(
            std::remove_if(s.effects.begin(), s.effects.end(),
                [&](const ActiveEffect& e){
                    if (e.hoursRemaining <= 0) {
                        cout << "[Weather] " << e.description << " has cleared.\n";
                        return true;
                    }
                    return false;
                }),
            s.effects.end()
        );
    }

    // ---- Power & resource update -------------------------------------------

    void advanceHours(int hours) {
        hours = std::max(0, hours);
        for (int i = 0; i < hours; ++i) {
            maybeSpawnEvents();
            simulateHour();
            tickEffects();
            ++s.hour;
        }
        // After advancing, show a compact status line:
        cout << "Advanced " << hours << " " << pluralize("hour", hours)
             << ". Now Sol " << sol() << ", Hour " << hourOfSol() << ".\n";
    }

    void simulateHour() {
        // 1) Power production
        double producers = 0.0;
        double daylight = daylightFactor();
        double stormMult = stormSolarMultiplier();

        for (const auto& b : s.buildings) {
            if (!b.active) continue;
            const auto& sp = getSpec(b.type);

            if (sp.powerProdConst > 0.0) {
                producers += sp.powerProdConst;
            }
            if (sp.powerProdDay > 0.0) {
                producers += sp.powerProdDay * daylight * stormMult;
            }
        }

        // 2) Compute critical & non-critical consumption
        double critical = s.population * PWR_PER_COLONIST;
        double noncrit = 0.0;

        for (const auto& b : s.buildings) {
            if (!b.active) continue;
            const auto& sp = getSpec(b.type);
            if (sp.powerCons <= 0.0 || !sp.needsPower) continue;
            if (sp.isCriticalLoad) critical += sp.powerCons;
            else                   noncrit  += sp.powerCons;
        }

        // 3) Allocate power and adjust battery
        double available = s.res.powerStored + producers - critical;
        bool blackout = available < 0.0; // unmet critical load

        double noncritEff = 0.0; // how much of non-critical we can run this hour
        if (!blackout) {
            if (noncrit <= 0.0) noncritEff = 0.0;
            else noncritEff = clampv( available / noncrit, 0.0, 1.0 );
        }

        // compute how much power is used this hour
        double used = critical + noncrit * noncritEff;
        // battery delta: producers - used
        s.res.powerStored += (producers - used);
        s.res.powerStored = clampv(s.res.powerStored, 0.0, s.res.batteryCapacity);

        // 4) Resource flows from buildings — gated by power availability
        double waterDelta  = 0.0;
        double oxygenDelta = 0.0;
        double foodDelta   = 0.0;

        for (const auto& b : s.buildings) {
            if (!b.active) continue;
            const auto& sp = getSpec(b.type);
            if (sp.waterFlow == 0.0 && sp.oxygenFlow == 0.0 && sp.foodFlow == 0.0) continue;

            double eff = 1.0;
            if (sp.needsPower) {
                eff = sp.isCriticalLoad ? (blackout ? 0.0 : 1.0) : noncritEff;
            }

            waterDelta  += sp.waterFlow  * eff;
            oxygenDelta += sp.oxygenFlow * eff;
            foodDelta   += sp.foodFlow   * eff;
        }

        // 5) Population consumption
        waterDelta  -= s.population * WAT_PER_COLONIST;
        oxygenDelta -= s.population * O2_PER_COLONIST;
        foodDelta   -= s.population * FOOD_PER_COLONIST;

        // 6) Apply resource deltas & clamp (no negative stores)
        s.res.water  = std::max(0.0, s.res.water  + waterDelta);
        s.res.oxygen = std::max(0.0, s.res.oxygen + oxygenDelta);
        s.res.food   = std::max(0.0, s.res.food   + foodDelta);

        // 7) Morale adjustments (simple)
        //    Positive drift if everything is fine, else penalties.
        double moraleDelta = 0.0;

        auto hoursOf = [&](double store, double ratePerHour){
            if (ratePerHour <= 0.0) return 9999.0; // not consuming; "infinite"
            return (store / ratePerHour);
        };

        double hFood  = hoursOf(s.res.food,   s.population * FOOD_PER_COLONIST);
        double hWater = hoursOf(s.res.water,  s.population * WAT_PER_COLONIST);
        double hO2    = hoursOf(s.res.oxygen, s.population * O2_PER_COLONIST);

        if (blackout)         moraleDelta -= 0.04;
        if (hFood  < 24.0)    moraleDelta -= 0.02;
        if (hWater < 24.0)    moraleDelta -= 0.02;
        if (hO2    < 24.0)    moraleDelta -= 0.03;

        if (!blackout && hFood > 72 && hWater > 72 && hO2 > 72 && s.res.powerStored > s.res.batteryCapacity * 0.5) {
            moraleDelta += 0.01; // slow recovery when things are comfortable
        }

        // Overcrowding
        if (s.population > s.housingCapacity) moraleDelta -= 0.02;

        s.morale = clampv(s.morale + moraleDelta, 0.0, 1.0);

        // 8) Persist last power report for UI
        s.lastPower.producers = producers;
        s.lastPower.criticalDemand = critical;
        s.lastPower.nonCriticalDemand = noncrit;
        s.lastPower.nonCriticalEff = noncritEff;
        s.lastPower.blackout = blackout;

        // 9) Very rough failure checks (starter)
        if (s.res.oxygen <= 0.0 || s.res.food <= 0.0 || s.res.water <= 0.0) {
            // Optional: implement health damage or casualties here
            // For starter, just log a warning:
            cout << "[Warning] Critical shortage: ";
            if (s.res.oxygen <= 0.0) cout << "Oxygen ";
            if (s.res.water  <= 0.0) cout << "Water ";
            if (s.res.food   <= 0.0) cout << "Food ";
            cout << "!\n";
        }
    }
};

// ----------- Entry point -----------------------------------------------------

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    Game g;
    g.runCLI();
    return 0;
}
