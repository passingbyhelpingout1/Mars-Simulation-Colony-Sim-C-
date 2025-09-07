#include "power.hpp"
#include <algorithm>
#include <cmath>

namespace mars {

// Cosine-smoothed daylight curve: 0 at night, 1 at local noon
double daylightFactor(int hourOfSol) {
    // Map hour (0..23) onto [0, 2π), shift so noon peaks at 1.0
    double theta = (static_cast<double>(hourOfSol) / SOL_HOURS) * 2.0 * PI;
    double c = std::cos(theta - PI); // =1 at noon (hour 12)
    if (c < 0.0) c = 0.0;
    return c; // 0..1 (soft twilight shoulders)
}

} // namespace mars
