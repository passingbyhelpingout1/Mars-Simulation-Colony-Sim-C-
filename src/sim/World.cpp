#include "World.h"
#include <cstddef>

using namespace sim;

static inline uint64_t fnv1a64_update(uint64_t h, const void* d, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(d);
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

uint64_t World::checksum() const noexcept {
    uint64_t h = 1469598103934665603ULL;
    h = fnv1a64_update(h, &tick, sizeof tick);
    h = fnv1a64_update(h, colonists.data(), colonists.size() * sizeof(Colonist));
    h = fnv1a64_update(h, habitats.data(),  habitats.size()  * sizeof(Habitat));
    return h;
}

// (serialize/deserialize implementations are straightforward; keep little-endian)
