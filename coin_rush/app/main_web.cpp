// Coin Rush's web entry point. Selected by CMake, so `main.cpp` keeps the
// desktop path with no #ifdef between them — as main_android.cpp does.
#include <cstdlib>
#include <memory>

#include "aether/app/web_host.hpp"
#include "aether/core/config.hpp"
#include "aether/core/log.hpp"
#include "coin_rush.hpp"

#ifndef AETHER_ASSET_DIR
#define AETHER_ASSET_DIR "."
#endif

using namespace aether;
using namespace game;

// `main` here does NOT run the game: it starts it. Returning schedules the
// first browser frame and hands control back to the page, which keeps the tab
// alive rather than ending the program (see RunWeb).
//
// NO CommandLine, and this is a hard constraint rather than a tidy-up. A web
// build links the no-exceptions libc++ (`-lc++-noexcept`), so ANY throw aborts
// the module with `Aborted(undefined)` and no message. Engine code never throws
// — but CLI11 does, on an unrecognised option, and every dev option is
// compiled out here. Measured: `sandbox.js --frames 60` aborts, while
// `sandbox.js --headless` runs fine.
//
// Nothing is lost. A page has no argv, and `--level` and the record/replay
// hooks are dev affordances this configuration does not contain.
int main() {
  // AETHER_ASSET_DIR is a MEMFS mount point here, not a build-machine path:
  // the tree is packed into the module's `.data` blob at link time, mounted at
  // `/assets` (games/coin_rush/CMakeLists.txt). Which is why this line is
  // unchanged from the desktop `main`: a preloaded archive whose virtual files
  // keep their relative paths looks like a filesystem, so nothing above
  // `platform` learns that a browser has no disk (GEA §7.2.2).
  Config config = app::LoadAppConfig(AETHER_ASSET_DIR);
  config.Set("asset_dir", AETHER_ASSET_DIR);

  const auto result =
      app::RunWeb(std::make_unique<CoinRushGame>(0, "", ""), config);
  if (!result) {
    LogError("Coin Rush failed [{}]: {}", app::ToString(result.error().error),
             result.error().cause.message);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
