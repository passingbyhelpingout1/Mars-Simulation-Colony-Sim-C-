#ifndef MARS_EFFECTS_HPP
#define MARS_EFFECTS_HPP

#include <algorithm>   // std::remove_if
#include <cassert>     // assert (debug invariant)
#include <iostream>    // std::cout
#include <string>      // std::string

namespace effects {

// Erase–remove idiom to prune expired effects from a container.
// Requirements for element type 'E':
//   - E has a numeric 'hoursRemaining' field
//   - E has a 'description' field (string-like)
// Prints a "has cleared" line when not in forecast mode.
template <typename EffectsContainer>
void pruneExpiredEffects(EffectsContainer& effects, bool forecastMode)
{
    auto newEnd = std::remove_if(effects.begin(), effects.end(),
        [&](const auto& e) {
            if (e.hoursRemaining <= 0) {
                if (!forecastMode) {
                    std::cout << "[Weather] " << e.description << " has cleared.\n";
                }
                return true; // remove this expired effect
            }
            return false;
        });

    effects.erase(newEnd, effects.end());

#ifndef NDEBUG
    // Defensive check: nothing with non-positive remaining time should survive.
    for (const auto& e : effects) {
        assert(e.hoursRemaining > 0 && "pruneExpiredEffects: found non-positive hoursRemaining after pruning");
    }
#endif
}

} // namespace effects

#endif // MARS_EFFECTS_HPP
