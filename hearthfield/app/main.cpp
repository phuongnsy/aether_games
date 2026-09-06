#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>

#include "aether/app/app.hpp"
#include "aether/app/command_line.hpp"
#include "aether/core/config.hpp"
#include "aether/core/log.hpp"
#include "hearthfield.hpp"
#include "hf/content/farm.hpp"
#include "hf/runtime/tick.hpp"

#ifndef AETHER_ASSET_DIR
#define AETHER_ASSET_DIR "."
#endif

int main(int argc, char** argv) {
  aether::Config config = aether::app::LoadAppConfig(AETHER_ASSET_DIR);
  config.Set("asset_dir", AETHER_ASSET_DIR);

  int columns = static_cast<int>(hearthfield::content::kDefaultColumns);
  bool sow = false;
  bool autopilot = false;
  int offline = -1;
  bool no_save = false;
  bool rain = false;
  std::string screen;
  bool build_mode = false;
  int island = -1;  // -1: whichever island the save was left standing on
  int travel = -1;  // -1: stay put
  bool ui_lint = false;
  aether::app::CommandLine cli("hearthfield");
  cli.Option("--grid", &columns,
             "board width in plots for a NEW farm (a save sizes its own; 15 "
             "gives 225 — the check-2 measurement)");
  cli.Option("--sow", &sow,
             "plant every EMPTY plot at load, for the measurement");
  cli.Option("--autopilot", &autopilot,
             "play a scripted session so --frames is reproducible");
  cli.Option("--offline", &offline,
             "pretend the game was shut this many seconds — check 4 without "
             "the ten-minute wait");
  cli.Option("--no-save", &no_save,
             "do not read or write the save file (leaves a real farm alone)");
  cli.Option("--rain", &rain,
             "force the sky on — a capture must not depend on which tick it "
             "started at (weather is a pure function of the clock)");
  cli.Option("--build", &build_mode,
             "start in build mode, ghost following the pointer. The --screen "
             "precedent: a capture has no hand on the mouse.");
  cli.Option("--screen", &screen,
             "open a screen at load: barn | shop | orders. For capturing one "
             "without a hand on the mouse.");
  cli.Option(
      "--island", &island,
      "load this island instead of the one the save was left on: 0 is "
      "the hub. This is where the game STARTS; --travel is how it moves. "
      "It does NOT unlock one.");
  cli.Option("--ui-lint", &ui_lint,
             "run the layout rules over every screen at every canvas size and "
             "exit non-zero on a violation; pair with --headless");
  cli.Option("--travel", &travel,
             "begin a crossing to this island at load, so a flight can be "
             "captured headless (a keyboard cannot press T in a capture). "
             "UNLOCKS its target for free — a dev flag, not a verb.");
  if (auto code = cli.ParseOrExit(argc, argv, config)) {
    return *code;
  }
  // THE LINT MUST TERMINATE EITHER WAY. A violation fails Load and exits
  // non-zero; a clean run would otherwise fall through into the normal loop and
  // sit there forever, which is a gate that hangs CI rather than passing it.
  // One frame is the existing mechanism `--frames` already drives.
  if (ui_lint) {
    config.Set("dev.max_frames", 1.0);
    // AND IT MUST NOT TRIP THE CRASH GUARD. A failing lint ends before
    // rendering on purpose, which is exactly the signature ADR-0121's guard
    // watches for — so three red lint runs left the game refusing to start at
    // all, and the gate had bricked the thing it guards.
    config.Set("app.crash_guard", false);
  }
  if (columns < 1 || columns > 64) {
    aether::LogError("hearthfield: --grid must be 1..64, got {}", columns);
    return EXIT_FAILURE;
  }

  // THE TICK RATE AND THE FIXED STEP MUST AGREE, and this is the only place
  // that can check it: `fixed_dt` is a config key the CLI may also override, so
  // the check belongs after parsing and before the loop starts.
  //
  // A pinned value in a comment is a wish. ADR-0106 makes the world's tick
  // counter absolute and a save carries it, so a mismatched step silently
  // rescales every crop timer AND changes what a file already on disk means.
  const auto fixed_dt = static_cast<double>(
      config.GetOr("fixed_dt", 1.0f / hearthfield::runtime::kTicksPerSecond));
  const double want = 1.0 / hearthfield::runtime::kTicksPerSecond;
  if (std::abs(fixed_dt - want) > 1e-6) {
    aether::LogError(
        "hearthfield: fixed_dt is {:.9f} but runtime::kTicksPerSecond is {}, "
        "which wants {:.9f}. Crop timers are absolute tick counts (ADR-0106), "
        "so this would rescale the whole economy.",
        fixed_dt, hearthfield::runtime::kTicksPerSecond, want);
    return EXIT_FAILURE;
  }

  const auto result =
      aether::app::Run(std::make_unique<hearthfield::app::HearthfieldGame>(
                           static_cast<aether::U32>(columns), sow, autopilot,
                           static_cast<aether::I64>(offline), no_save, screen,
                           rain, build_mode, island, travel, ui_lint),
                       config);
  if (!result) {
    aether::LogError("hearthfield failed [{}]: {}",
                     aether::app::ToString(result.error().error),
                     result.error().cause.message);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
