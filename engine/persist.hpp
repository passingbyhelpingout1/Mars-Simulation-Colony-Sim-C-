#pragma once
#include "state.hpp"
#include <string>

namespace mars {

bool saveGame(const GameState& s, const std::string& path);
bool loadGame(GameState& s, const std::string& path);

} // namespace mars
