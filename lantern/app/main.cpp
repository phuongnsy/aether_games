#include <cstdlib>
#include <memory>
#include <string>

#include "aether/app/app.hpp"
#include "aether/app/command_line.hpp"
#include "aether/core/config.hpp"
#include "aether/core/log.hpp"
#include "lantern.hpp"

#ifndef AETHER_ASSET_DIR
#define AETHER_ASSET_DIR "."
#endif

int main(int argc, char** argv) {
  aether::Config config = aether::app::LoadAppConfig(AETHER_ASSET_DIR);
  config.Set("asset_dir", AETHER_ASSET_DIR);
  bool demo = false;
  int lit = 0;
  std::string weather = "rain";
  int soft = 1;
  float soft_fade = 1.0f;
  aether::app::CommandLine cli("lantern");
  cli.Option("--demo", &demo,
             "walk the scripted route and light the first lantern");
  cli.Option("--lit", &lit,
             "light this many lanterns at load (measuring the shadow cost)");
  cli.Option("--weather", &weather, "off | rain (default) | snow");
  cli.Option("--soft", &soft,
             "soft particles: 1 (default) fades precipitation where it meets "
             "geometry, 0 off. Costs ~31 us GPU and 9 draws.");
  cli.Option("--soft-fade", &soft_fade,
             "fade distance in world units — how thick a drop pretends to be");
  if (auto code = cli.ParseOrExit(argc, argv, config)) {
    return *code;
  }
  lantern::weather::Mode mode = lantern::weather::Mode::kRain;
  if (weather == "off") {
    mode = lantern::weather::Mode::kOff;
  } else if (weather == "snow") {
    mode = lantern::weather::Mode::kSnow;
  }
  const auto result =
      aether::app::Run(std::make_unique<lantern::app::LanternGame>(
                           demo, lit, mode, soft != 0, soft_fade),
                       config);
  if (!result) {
    aether::LogError("lantern failed [{}]: {}",
                     aether::app::ToString(result.error().error),
                     result.error().cause.message);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
