#include <iostream>
#include "core/sim.hpp"
// Optional: parse --seed, --record, --replay as you already advertise in README/diff.

int main(int argc, char** argv) {
    Game g;
    // parseArgs(...); // set seed, load replay, schedule orders, etc.
    for (int i = 0; i < 24; ++i) g.step(); // run one sol as a smoke test
    std::cout << "Hour=" << g.s.hour << " Metals=" << g.s.metals << "\n";
}
