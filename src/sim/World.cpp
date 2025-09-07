#include "World.h"
#include "determinism/state_hash.hpp"

using namespace sim;

uint64_t World::checksum() const noexcept {
    uint64_t h = 1469598103934665603ULL;
    h = sim::det::u64_le(h, tick);
    h = sim::det::fnv1a64(h, colonists.data(), colonists.size() * sizeof(Colonist));
    h = sim::det::fnv1a64(h, habitats.data(), habitats.size() * sizeof(Habitat));
    return h;
}
