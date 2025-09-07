#include "persist.hpp"
#include <fstream>
#include <sstream>

namespace mars {

static std::string serializeRng(const std::mt19937& rng) {
    std::ostringstream oss;
    oss << rng;
    return oss.str();
}

static bool deserializeRng(std::mt19937& rng, const std::string& s) {
    std::istringstream iss(s);
    iss >> rng;
    return static_cast<bool>(iss);
}

bool saveGame(const GameState& s, const std::string& path) {
    std::ofstream f(path);
    if (!f) return false;

    f << "hour=" << s.hour << "\n";
    f << "colonists=" << s.colonists << "\n";
    f << "solarPanels=" << s.solarPanels << "\n";
    f << "batteries=" << s.batteries << "\n";
    f << "labs=" << s.labs << "\n";
    f << "powerStored=" << s.res.powerStored << "\n";
    f << "rng=" << serializeRng(s.rng) << "\n";
    f << "weather_dustStorm=" << (s.weather.dustStorm ? 1 : 0) << "\n";
    f << "weather_dustStormHours=" << s.weather.dustStormHours << "\n";
    f << "weather_solarMultiplier=" << s.weather.solarMultiplier << "\n";
    return true;
}

bool loadGame(GameState& s, const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;

    GameState tmp = s; // in case of partial read
    std::string line;
    std::string rngState;

    while (std::getline(f, line)) {
        auto pos = line.find('=');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);

        if (key == "hour") tmp.hour = std::stoi(val);
        else if (key == "colonists") tmp.colonists = std::stoi(val);
        else if (key == "solarPanels") tmp.solarPanels = std::stoi(val);
        else if (key == "batteries") tmp.batteries = std::stoi(val);
        else if (key == "labs") tmp.labs = std::stoi(val);
        else if (key == "powerStored") tmp.res.powerStored = std::stod(val);
        else if (key == "rng") rngState = val;
        else if (key == "weather_dustStorm") tmp.weather.dustStorm = (std::stoi(val) != 0);
        else if (key == "weather_dustStormHours") tmp.weather.dustStormHours = std::stoi(val);
        else if (key == "weather_solarMultiplier") tmp.weather.solarMultiplier = std::stod(val);
    }

    if (!rngState.empty()) {
        if (!deserializeRng(tmp.rng, rngState)) {
            return false;
        }
    }

    recomputePowerCapacity(tmp);
    // Clamp stored energy to capacity
    if (tmp.res.powerStored > tmp.res.powerCapKWh) tmp.res.powerStored = tmp.res.powerCapKWh;
    s = std::move(tmp);
    return true;
}

} // namespace mars
