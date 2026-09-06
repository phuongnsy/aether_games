#include <cstdlib>
#include <memory>

#include "aether/app/app.hpp"
#include "aether/app/command_line.hpp"
#include "aether/core/config.hpp"
#include "aether/core/log.hpp"
#include "tideworn.hpp"

#ifndef AETHER_ASSET_DIR
#define AETHER_ASSET_DIR "."
#endif

int main(int argc, char** argv) {
  aether::Config config = aether::app::LoadAppConfig(AETHER_ASSET_DIR);
  config.Set("asset_dir", AETHER_ASSET_DIR);
  tideworn::app::Options options;
  bool swim = false;
  aether::app::CommandLine cli("tideworn",
                               "sail an open sea whose weather is a schedule");
  cli.Option("--time-scale", &options.time_scale,
             "compresses the voyage clock: the sea-state schedule is 600 s per "
             "cycle, and a bench or capture wants the storm sooner");
  cli.Option("--pitch", &options.pitch,
             "starting camera pitch in radians; NEGATIVE dives the orbit "
             "below the surface (the underwater path's reproducible A/B)");
  cli.Option("--yaw", &options.yaw, "starting camera yaw in radians");
  cli.Option("--distance", &options.distance,
             "starting camera distance in metres");
  cli.Option("--swim", &swim,
             "boot in the first-person swim camera, beside the boat (V "
             "toggles it at runtime)");
  cli.Option("--depth", &options.depth,
             "how deep the SWIM camera starts, in metres below the surface; "
             "--pitch/--yaw aim it (positive pitch looks down), so one command "
             "can frame the surface from below, the level fog, or the deep");
  cli.Option("--start-clock", &options.start_clock,
             "pre-run the voyage this many seconds before the first frame — "
             "a swim capture wants daylight and a settled school");
  if (auto code = cli.ParseOrExit(argc, argv, config)) {
    return *code;
  }
  options.swim = swim;
  const auto result = aether::app::Run(
      std::make_unique<tideworn::app::TidewornGame>(options), config);
  if (!result) {
    aether::LogError("tideworn failed [{}]: {}",
                     aether::app::ToString(result.error().error),
                     result.error().cause.message);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
