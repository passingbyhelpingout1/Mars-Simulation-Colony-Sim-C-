#include "sim/Simulation.h"
#include <cassert>
int main() {
    sim::Simulation a(12345), b(12345);
    sim::Input in{};
    for (int i = 0; i < 10'000; ++i) {
        a.tick(in);
        b.tick(in);
    }
    assert(a.world().checksum() == b.world().checksum());
    return 0;
}
