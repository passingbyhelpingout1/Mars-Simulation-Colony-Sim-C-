// mars_colony.cpp
//
// Deterministic, single-file headless runner + tiny interactive fallback.
// Designed to compile standalone under C++17 on Linux and Windows.
//
// CI contract (per .github/workflows/ci.yml):
//   - Build with:  $CXX -std=c++17 -O2 -Wall -Wextra -o mars mars_colony.cpp
//   - Run:         ./mars --selftest
// This file always defines main() and implements --selftest.
//
// Headless usage examples:
//   ./mars --replay path/to/file --hours 12 --seed 123 --hash-only
//   ./mars --minutes 90 --hash-only
//
// Output (headless):
//   Either a single line:
//     STATE_HASH=<16-digit UPPERCASE HEX>\n
//   or a short status + that line when --hash-only is not set.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS 1
#endif
#include <inttypes.h> // PRIX64, PRIu64

#ifdef _WIN32
  #include <io.h>
  #include <fcntl.h>
#endif

// ----------------------- Deterministic helpers -------------------------------

namespace detail {

// FNV-1a 64-bit
static constexpr std::uint64_t FNV_OFFSET_BASIS = 1469598103934665603ULL;
static constexpr std::uint64_t FNV_PRIME        = 1099511628211ULL;

inline std::uint64_t fnv1a64_update(std::uint64_t h, const void* data, size_t n) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) {
        h ^= p[i];
        h *= FNV_PRIME;
    }
    return h;
}

// Canonical little-endian updates for integer types (cross‑arch stability)
inline std::uint64_t fnv1a64_u32_le(std::uint64_t h, std::uint32_t v) {
    unsigned char b[4] = {
        static_cast<unsigned char>( v        & 0xFFu),
        static_cast<unsigned char>((v >> 8)  & 0xFFu),
        static_cast<unsigned char>((v >> 16) & 0xFFu),
        static_cast<unsigned char>((v >> 24) & 0xFFu)
    };
    return fnv1a64_update(h, b, 4);
}

inline std::uint64_t fnv1a64_u64_le(std::uint64_t h, std::uint64_t v) {
    unsigned char b[8] = {
        static_cast<unsigned char>( v        & 0xFFu),
        static_cast<unsigned char>((v >> 8)  & 0xFFu),
        static_cast<unsigned char>((v >> 16) & 0xFFu),
        static_cast<unsigned char>((v >> 24) & 0xFFu),
        static_cast<unsigned char>((v >> 32) & 0xFFu),
        static_cast<unsigned char>((v >> 40) & 0xFFu),
        static_cast<unsigned char>((v >> 48) & 0xFFu),
        static_cast<unsigned char>((v >> 56) & 0xFFu)
    };
    return fnv1a64_update(h, b, 8);
}

// Stream file into FNV-1a hash; also capture a small byte sample (head+tail)
// for deterministic tick mixing when --replay is used.
inline std::uint64_t hash_file_streaming(const std::string& path,
                                         std::vector<unsigned char>* sample,
                                         size_t sample_max = 4096) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return 0ULL;

    const size_t BUF = 64 * 1024;
    std::vector<char> buf(BUF);

    std::uint64_t h = FNV_OFFSET_BASIS;

    const size_t head_max = sample ? sample_max / 2 : 0;
    const size_t tail_max = sample ? (sample_max - head_max) : 0;

    std::vector<unsigned char> head; head.reserve(head_max);
    std::vector<unsigned char> tail; tail.reserve(tail_max);

    for (;;) {
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        std::streamsize n = f.gcount();
        if (n <= 0) break;

        h = fnv1a64_update(h, buf.data(), static_cast<size_t>(n));

        if (sample) {
            const unsigned char* ub = reinterpret_cast<const unsigned char*>(buf.data());
            size_t nn = static_cast<size_t>(n);

            // Fill head first
            size_t can_copy_head = std::min(nn, head_max - head.size());
            if (can_copy_head > 0) head.insert(head.end(), ub, ub + can_copy_head);

            // Then maintain a rolling tail
            size_t start_tail = can_copy_head;
            if (nn > start_tail) {
                size_t tcount = nn - start_tail;
                tail.insert(tail.end(), ub + start_tail, ub + start_tail + tcount);
                if (tail.size() > tail_max) {
                    size_t drop = tail.size() - tail_max;
                    tail.erase(tail.begin(), tail.begin() + static_cast<std::ptrdiff_t>(drop));
                }
            }
        }
    }

    if (sample) {
        sample->clear();
        sample->insert(sample->end(), head.begin(), head.end());
        sample->insert(sample->end(), tail.begin(), tail.end());
    }
    return h;
}

// Tiny deterministic PRNG
inline std::uint32_t xorshift32(std::uint32_t& s) {
    s ^= (s << 13);
    s ^= (s >> 17);
    s ^= (s << 5);
    return s;
}

} // namespace detail

// ----------------------- Toy simulation model --------------------------------

struct World {
    std::uint64_t tick      = 0;      // "minute" ticks
    std::int32_t  oxygen_mg = 20000;
    std::int32_t  co2_mg    = 0;
    std::int32_t  temp_milK = 293000; // 293 K
    std::int32_t  power_mW  = 15000;
    std::uint32_t rng       = 0x9E3779B9u;
};

inline void step(World& w) {
    ++w.tick;

    // very simple life support and thermal drift
    w.oxygen_mg -= 5; if (w.oxygen_mg < 0) w.oxygen_mg = 0;
    w.co2_mg    += 3;
    w.temp_milK += (w.power_mW >= 12000) ? 1 : -1;

    // deterministic power flicker
    std::uint32_t r = detail::xorshift32(w.rng);
    int delta = static_cast<int>(r % 21u) - 10; // [-10,+10] mW
    w.power_mW += delta;
    if (w.power_mW < 0) w.power_mW = 0;
}

// Cross‑arch stable checksum of state (explicit LE canonicalization)
inline std::uint64_t world_checksum(const World& w) {
    using namespace detail;
    std::uint64_t h = FNV_OFFSET_BASIS;
    h = fnv1a64_u64_le(h, w.tick);
    h = fnv1a64_u32_le(h, static_cast<std::uint32_t>(w.oxygen_mg));
    h = fnv1a64_u32_le(h, static_cast<std::uint32_t>(w.co2_mg));
    h = fnv1a64_u32_le(h, static_cast<std::uint32_t>(w.temp_milK));
    h = fnv1a64_u32_le(h, static_cast<std::uint32_t>(w.power_mW));
    h = fnv1a64_u32_le(h, w.rng);
    return h;
}

// ----------------------- CLI parsing -----------------------------------------

struct Options {
    std::string   replay_path;
    std::uint64_t ticks     = 0;        // authoritative tick count
    std::uint32_t seed      = 0x9E3779B9u;
    bool          hash_only = false;
    bool          headless  = false;
    bool          selftest  = false;
    bool          ok        = true;     // false -> print usage and exit 0
};

inline bool parse_u64(const char* s, std::uint64_t& out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    unsigned long long v = std::strtoull(s, &end, 10);
    if (end == s || *end != '\0') return false;
    out = static_cast<std::uint64_t>(v);
    return true;
}

inline bool parse_u64_to_u32_seed(const char* s, std::uint32_t& out) {
    std::uint64_t v64 = 0;
    if (!parse_u64(s, v64)) return false;
    out = static_cast<std::uint32_t>((v64 & 0xFFFFFFFFu) ^ (v64 >> 32));
    return true;
}

inline bool checked_mul(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
    if (a == 0 || b == 0) { out = 0; return true; }
    if (a > std::numeric_limits<std::uint64_t>::max() / b) return false;
    out = a * b;
    return true;
}

Options parse_args(int argc, char** argv) {
    Options o{};

    if (const char* env = std::getenv("MARS_DEFAULT_SEED")) {
        (void)parse_u64_to_u32_seed(env, o.seed);
    }

    std::uint64_t hours=0, minutes=0, ticks=0;
    bool hours_set=false, minutes_set=false, ticks_set=false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];

        if ((a == "--replay" || a == "-r") && i + 1 < argc) {
            o.replay_path = argv[++i];
            o.headless = true;
        } else if ((a == "--hours" || a == "-H") && i + 1 < argc) {
            if (!parse_u64(argv[++i], hours)) { o.ok = false; break; }
            hours_set = true; o.headless = true;
        } else if ((a == "--minutes" || a == "-M") && i + 1 < argc) {
            if (!parse_u64(argv[++i], minutes)) { o.ok = false; break; }
            minutes_set = true; o.headless = true;
        } else if ((a == "--ticks" || a == "-T") && i + 1 < argc) {
            if (!parse_u64(argv[++i], ticks)) { o.ok = false; break; }
            ticks_set = true; o.headless = true;
        } else if (a == "--seed" && i + 1 < argc) {
            if (!parse_u64_to_u32_seed(argv[++i], o.seed)) { o.ok = false; break; }
            o.headless = true;
        } else if (a == "--hash-only" || a == "-q") {
            o.hash_only = true; o.headless = true;
        } else if (a == "--selftest") {
            o.selftest  = true; o.headless = true;
        } else if (a == "--headless") {
            o.headless = true;
        } else if (a == "--help" || a == "-h" || a == "/?") {
            o.ok = false; break;
        } else {
            // Ignore unknown flags for CI robustness.
            // (No change to o.headless.)
        }
    }

    if (ticks_set) {
        o.ticks = ticks;
    } else if (minutes_set) {
        o.ticks = minutes;            // 1 tick = 1 minute
    } else if (hours_set) {
        std::uint64_t t = 0;
        if (!checked_mul(hours, 60ULL, t)) { o.ok = false; }
        else o.ticks = t;
    }

    return o;
}

void print_usage() {
    std::cout
        << "Usage:\n"
        << "  mars --replay <file> [--hours N | --minutes N | --ticks N] [--seed S] [--hash-only]\n"
        << "  mars --selftest\n"
        << "  mars  (interactive fallback)\n";
}

// ----------------------- Headless and interactive ----------------------------

int run_headless(const Options& opt) {
    // If selftest is requested, run a deterministic scenario and finish.
    if (opt.selftest) {
        // Build a tiny deterministic run: combine seed + a small synthetic "replay" wheel.
        std::vector<unsigned char> wheel;
        // Populate wheel with a simple pattern to ensure some mixing:
        for (unsigned i = 0; i < 256; ++i) wheel.push_back(static_cast<unsigned char>(i));

        World w{};
        // Seed depends only on fixed value in selftest to keep it platform-stable
        w.rng = 0x12345678u ^ 0x9E3779B9u;

        // Run 600 ticks (~10 hours)
        for (int i = 0; i < 600; ++i) {
            if (!wheel.empty()) w.rng ^= wheel[static_cast<size_t>(i) % wheel.size()];
            step(w);
        }

        const std::uint64_t hash = world_checksum(w);
        std::printf("STATE_HASH=%016" PRIX64 "\n", static_cast<unsigned long long>(hash));
        std::fflush(stdout);
        return 0;
    }

    // Normal headless: derive seed/salt from replay file if present
    std::vector<unsigned char> sample;
    std::uint64_t replay_hash = 0ULL;

    if (!opt.replay_path.empty()) {
        replay_hash = detail::hash_file_streaming(opt.replay_path, &sample, 4096);
    }

    World w{};
    w.rng = opt.seed
          ^ static_cast<std::uint32_t>(replay_hash)
          ^ static_cast<std::uint32_t>(replay_hash >> 32);

    // Small deterministic perturbations from replay hash to vary initial state
    w.oxygen_mg += static_cast<std::int32_t>(replay_hash & 1023ULL);
    w.power_mW  += static_cast<std::int32_t>((replay_hash >> 10) & 2047ULL) - 1024;

    const size_t wheel = sample.size();
    for (std::uint64_t i = 0; i < opt.ticks; ++i) {
        if (wheel) w.rng ^= sample[static_cast<size_t>(i % wheel)];
        step(w);
    }

    const std::uint64_t hash = world_checksum(w);

    if (opt.hash_only) {
        std::printf("STATE_HASH=%016" PRIX64 "\n", static_cast<unsigned long long>(hash));
        std::fflush(stdout);
    } else {
        std::cout << "Ticks: " << opt.ticks
                  << "  O2=" << w.oxygen_mg << " mg"
                  << "  CO2=" << w.co2_mg    << " mg"
                  << "  T="   << w.temp_milK << " mK"
                  << "  P="   << w.power_mW  << " mW\n";
        std::printf("STATE_HASH=%016" PRIX64 "\n", static_cast<unsigned long long>(hash));
        std::fflush(stdout);
    }
    return 0;
}

int run_interactive() {
    World w{};
    std::cout << "Mars Colony (CLI fallback)\n"
              << "Commands: t|tick, s|status, q|quit\n";

    for (std::string cmd; ; ) {
        std::cout << "> " << std::flush;
        if (!(std::cin >> cmd)) break;

        if (cmd == "q" || cmd == "quit") {
            break;
        } else if (cmd == "t" || cmd == "tick") {
            step(w);
            std::cout << "tick=" << w.tick << " (stepped)\n";
        } else if (cmd == "s" || cmd == "status") {
            std::printf("tick=%" PRIu64 " O2=%dmg CO2=%dmg T=%dmK P=%dmW STATE_HASH=%016" PRIX64 "\n",
                        static_cast<unsigned long long>(w.tick),
                        w.oxygen_mg, w.co2_mg, w.temp_milK, w.power_mW,
                        static_cast<unsigned long long>(world_checksum(w)));
            std::fflush(stdout);
        } else {
            std::cout << "unknown command: " << cmd << "\n";
        }
    }
    return 0;
}

// ----------------------- Program entry ---------------------------------------

int main(int argc, char** argv) {
#ifdef _WIN32
    // Keep exact byte-for-byte outputs on Windows (avoid CRLF transformation).
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif

    Options opt = parse_args(argc, argv);

    if (!opt.ok) {
        print_usage();
        return 0;
    }

    // Headless mode if any headless-ish option was provided.
    const bool headless_requested = opt.headless || opt.selftest;
    if (headless_requested)
        return run_headless(opt);

    // Otherwise drop into a tiny, robust interactive shell
    return run_interactive();
}
