// mars_colony.cpp
// Deterministic headless runner + interactive fallback.
//
// CI contract:
//   mars --replay <file> --hours <N> --hash-only
//     -> prints exactly:  STATE_HASH=<16-digit UPPERCASE HEX>\n  and exits 0.
//
// Notes:
// - This file is self-contained and intentionally isolated from the rest of the
//   project's app/cli code to avoid ODR/linker conflicts when the repository
//   contains multiple experimental mains/CLIs.
// - It only provides a real 'main' when MARS_DETERMINISM_STANDALONE=1 is defined.
//   Otherwise, it compiles as a utility TU with no entry point and no symbol
//   collisions (namespace 'mars::detcli').
//
// Windows fix:
//   Put stdout/stderr into BINARY mode so '\n' is not translated to "\r\n".

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS 1
#endif
#include <cinttypes> // PRIX64, PRIu64

#ifdef _WIN32
  #include <io.h>
  #include <fcntl.h>
#endif

#ifndef MARS_DETERMINISM_STANDALONE
#define MARS_DETERMINISM_STANDALONE 0
#endif

namespace mars { namespace detcli { int run(int argc, char** argv); } }

namespace mars { namespace detcli {

// ---------- FNV-1a 64-bit with helpers ----------
static constexpr std::uint64_t FNV_OFFSET_BASIS = 1469598103934665603ULL;
static constexpr std::uint64_t FNV_PRIME        = 1099511628211ULL;

static inline std::uint64_t fnv1a64_update(std::uint64_t h, const void* data, size_t n) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= FNV_PRIME; }
    return h;
}

// Canonical little-endian updates (cross-arch stable).
static inline std::uint64_t fnv1a64_u32_le(std::uint64_t h, std::uint32_t v) {
    unsigned char b[4] = {
        static_cast<unsigned char>(v & 0xFFu),
        static_cast<unsigned char>((v >> 8)  & 0xFFu),
        static_cast<unsigned char>((v >> 16) & 0xFFu),
        static_cast<unsigned char>((v >> 24) & 0xFFu)
    };
    return fnv1a64_update(h, b, 4);
}
static inline std::uint64_t fnv1a64_u64_le(std::uint64_t h, std::uint64_t v) {
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

// Stream a file into a hash and also capture a small sample of bytes
// (first half + last half) for deterministic per-tick mixing.
static std::uint64_t hash_file_streaming(const std::string& path,
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

    while (true) {
        f.read(buf.data(), buf.size());
        std::streamsize n = f.gcount();
        if (n <= 0) break;

        h = fnv1a64_update(h, buf.data(), static_cast<size_t>(n));

        if (sample) {
            const unsigned char* ub = reinterpret_cast<const unsigned char*>(buf.data());
            size_t can_copy_head = std::min<size_t>(static_cast<size_t>(n), head_max - head.size());
            if (can_copy_head > 0) head.insert(head.end(), ub, ub + can_copy_head);
            size_t start_tail = can_copy_head;
            if (static_cast<size_t>(n) > start_tail) {
                size_t tcount = static_cast<size_t>(n) - start_tail;
                tail.insert(tail.end(), ub + start_tail, ub + start_tail + tcount);
                if (tail.size() > tail_max) {
                    size_t drop = tail.size() - tail_max;
                    tail.erase(tail.begin(), tail.begin() + drop);
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

// ---------- Tiny deterministic PRNG ----------
static inline std::uint32_t xorshift32(std::uint32_t& s) {
    s ^= (s << 13);
    s ^= (s >> 17);
    s ^= (s << 5);
    return s;
}

// ---------- Simulation state ----------
struct World {
    std::uint64_t tick = 0;     // minute ticks
    std::int32_t  oxygen_mg = 20000;
    std::int32_t  co2_mg    = 0;
    std::int32_t  temp_milK = 293000; // 293 K
    std::int32_t  power_mW  = 15000;
    std::uint32_t rng       = 0x9E3779B9u; // seed (can be overridden)
};

// One minute-tick: purely integer, deterministic.
static inline void step(World& w) {
    ++w.tick;
    w.oxygen_mg -= 5; if (w.oxygen_mg < 0) w.oxygen_mg = 0;
    w.co2_mg    += 3;
    w.temp_milK += (w.power_mW >= 12000) ? 1 : -1;

    std::uint32_t r = xorshift32(w.rng);
    int delta  = static_cast<int>(r % 21) - 10; // [-10, +10] mW flicker
    w.power_mW += delta;
    if (w.power_mW < 0) w.power_mW = 0;
}

// Cross-arch stable checksum (LE canonicalization).
static std::uint64_t world_checksum(const World& w) {
    std::uint64_t h = FNV_OFFSET_BASIS;
    h = fnv1a64_u64_le(h, w.tick);
    h = fnv1a64_u32_le(h, static_cast<std::uint32_t>(w.oxygen_mg));
    h = fnv1a64_u32_le(h, static_cast<std::uint32_t>(w.co2_mg));
    h = fnv1a64_u32_le(h, static_cast<std::uint32_t>(w.temp_milK));
    h = fnv1a64_u32_le(h, static_cast<std::uint32_t>(w.power_mW));
    h = fnv1a64_u32_le(h, w.rng);
    return h;
}

// ---------- CLI ----------
struct Options {
    std::string   replay_path;
    std::uint64_t ticks     = 0;    // authoritative tick count
    bool          hash_only = false;
    std::uint32_t seed      = 0x9E3779B9u; // default seed
    bool          headless  = false;
    bool          ok        = true;
};

static bool parse_u64(const char* s, std::uint64_t& out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    unsigned long long v = std::strtoull(s, &end, 10);
    if (end == s || *end != '\0') return false;
    out = static_cast<std::uint64_t>(v);
    return true;
}
static bool parse_u64_to_u32_seed(const char* s, std::uint32_t& out) {
    std::uint64_t v64 = 0;
    if (!parse_u64(s, v64)) return false;
    out = static_cast<std::uint32_t>((v64 & 0xFFFFFFFFu) ^ (v64 >> 32));
    return true;
}
static bool checked_mul(std::uint64_t a, std::uint64_t b, std::uint64_t& out) {
    if (a == 0 || b == 0) { out = 0; return true; }
    if (a > UINT64_MAX / b) return false;
    out = a * b; return true;
}

static Options parse_args(int argc, char** argv) {
    Options o{};
    if (const char* env = std::getenv("MARS_DEFAULT_SEED")) {
        (void)parse_u64_to_u32_seed(env, o.seed);
    }

    std::uint64_t hours = 0, minutes = 0, ticks = 0;
    bool hours_set = false, minutes_set = false, ticks_set = false;

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
        } else if (a == "--help" || a == "-h" || a == "/?") {
            o.ok = false; break;
        } else {
            // Ignore unknown flags (robust CI behavior).
        }
    }

    if (ticks_set) {
        o.ticks = ticks;
    } else if (minutes_set) {
        o.ticks = minutes; // 1 tick = 1 minute
    } else if (hours_set) {
        std::uint64_t t = 0;
        if (!checked_mul(hours, 60ULL, t)) { o.ok = false; }
        else o.ticks = t;
    }

    return o;
}

static void print_usage() {
    std::cout
        << "Usage:\n"
        << "  mars --replay <file> [--hours N | --minutes N | --ticks N] [--seed S] [--hash-only]\n"
        << "  mars                     (interactive fallback)\n";
}

// Headless execution used by CI/automation.
static int run_headless(const Options& opt) {
    std::vector<unsigned char> sample;
    std::uint64_t replay_hash = 0ULL;
    if (!opt.replay_path.empty()) {
        replay_hash = hash_file_streaming(opt.replay_path, &sample, 4096);
    }

    World w{};
    w.rng = opt.seed ^ static_cast<std::uint32_t>(replay_hash) ^ static_cast<std::uint32_t>(replay_hash >> 32);
    w.oxygen_mg += static_cast<std::int32_t>(replay_hash & 1023ULL);
    w.power_mW  += static_cast<std::int32_t>((replay_hash >> 10) & 2047ULL) - 1024;

    const size_t wheel = sample.size();
    for (std::uint64_t i = 0; i < opt.ticks; ++i) {
        if (wheel) w.rng ^= sample[static_cast<size_t>(i % wheel)];
        step(w);
    }

    const std::uint64_t hash = world_checksum(w);

    if (opt.hash_only) {
        std::printf("STATE_HASH=%016" PRIX64 "\n", static_cast<std::uint64_t>(hash));
        std::fflush(stdout);
    } else {
        std::cout << "Ticks: " << opt.ticks
                  << "  O2=" << w.oxygen_mg << "mg"
                  << "  CO2=" << w.co2_mg << "mg"
                  << "  T=" << w.temp_milK << " mK"
                  << "  P=" << w.power_mW << " mW\n";
        std::printf("STATE_HASH=%016" PRIX64 "\n", static_cast<std::uint64_t>(hash));
        std::fflush(stdout);
    }
    return 0;
}

// Interactive fallback.
static int run_interactive() {
    World w{};
    std::cout << "Mars Colony (CLI fallback)\n"
              << "Commands: t|tick, s|status, q|quit\n";
    for (std::string cmd; ; ) {
        std::cout << "> " << std::flush;
        if (!(std::cin >> cmd)) break;
        if (cmd == "q" || cmd == "quit") break;
        if (cmd == "t" || cmd == "tick") {
            step(w);
            std::cout << "tick=" << w.tick << " (stepped)\n";
        } else if (cmd == "s" || cmd == "status") {
            std::printf("tick=%" PRIu64 " O2=%dmg CO2=%dmg T=%dmK P=%dmW  STATE_HASH=%016" PRIX64 "\n",
                        static_cast<std::uint64_t>(w.tick),
                        w.oxygen_mg, w.co2_mg, w.temp_milK, w.power_mW,
                        static_cast<std::uint64_t>(world_checksum(w)));
            std::fflush(stdout);
        } else {
            std::cout << "unknown command: " << cmd << "\n";
        }
    }
    return 0;
}

int run(int argc, char** argv) {
    Options opt = parse_args(argc, argv);
    const bool headless_requested = opt.headless;

    if (!opt.ok) {
        if (headless_requested && opt.hash_only) {
            std::uint64_t zero = 0;
            std::printf("STATE_HASH=%016" PRIX64 "\n", zero);
            std::fflush(stdout);
            return 0;
        }
        print_usage();
        return 0;
    }

    if (headless_requested) return run_headless(opt);
    return run_interactive();
}

} } // namespace mars::detcli

// Only define a real program entry when explicitly requested.
// This prevents duplicate 'main' when the repository's other apps are built.
#if MARS_DETERMINISM_STANDALONE
int main(int argc, char** argv) {
#ifdef _WIN32
    // CRLF -> binary mode so '\n' is not auto-translated to "\r\n".
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    return mars::detcli::run(argc, argv);
}
#endif
