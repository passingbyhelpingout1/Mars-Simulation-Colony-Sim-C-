// mars_colony.cpp
// Self-contained entry point with a safe fallback CLI implementation.
// If you link an external CLI (ui/cli/cli.cpp), define MARS_HAS_EXTERNAL_CLI
// so the fallback below is excluded.

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

// Forward declare the CLI entrypoint. We'll provide a fallback definition
// if no external CLI is linked in this build.
namespace mars { namespace cli { int run(); } }

#ifndef MARS_HAS_EXTERNAL_CLI
// --------------------
// Fallback CLI (tiny):
// --------------------
// Minimal interactive loop so single-file builds work and tests can run.
// Replace later with your real CLI in ui/cli/cli.cpp.
namespace mars { namespace cli {

struct WorldStub {
    uint64_t tick = 0;
    int32_t  oxygen_mg = 20'000;
    int32_t  power_mW  = 15'000;
};

static void step(WorldStub& w) {
    // tiny deterministic "sim" so the CLI does something
    ++w.tick;
    w.oxygen_mg -= 5;
    if (w.oxygen_mg < 0) w.oxygen_mg = 0;
    if (w.power_mW < 10'000) { /* pretend leak/penalty */ }
}

int run() {
    WorldStub w{};
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
            std::cout << "tick=" << w.tick
                      << " oxygen=" << w.oxygen_mg << "mg"
                      << " power="  << w.power_mW  << "mW\n";
        } else {
            std::cout << "unknown command: " << cmd << "\n";
        }
    }
    return 0;
}

} } // namespace mars::cli
#endif // MARS_HAS_EXTERNAL_CLI

// --------------------
// Program entry point:
// --------------------
int main(int /*argc*/ , char** /*argv*/) {
    return mars::cli::run();
}
