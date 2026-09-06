#include <cstdlib>
#include <memory>

#include "aether/app/app.hpp"
#include "aether/app/command_line.hpp"
#include "aether/core/config.hpp"
#include "aether/core/log.hpp"
#include "coin_rush.hpp"

#ifndef AETHER_ASSET_DIR
#define AETHER_ASSET_DIR "."
#endif

using namespace aether;
using namespace game;

int main(int argc, char** argv) {
  Config config = app::LoadAppConfig(AETHER_ASSET_DIR);
  config.Set("asset_dir", AETHER_ASSET_DIR);
  int start_level = 0;  // so a capture can open a level directly
  app::CommandLine cli("coin_rush", "Coin Rush");
  cli.Option("--level", &start_level, "level to start on");
  if (auto code = cli.ParseOrExit(argc, argv, config)) {
    return *code;
  }
  const char* record = std::getenv("COIN_RUSH_RECORD");
  const char* replay = std::getenv("COIN_RUSH_REPLAY");
  const auto result = app::Run(std::make_unique<CoinRushGame>(
                                   start_level, record != nullptr ? record : "",
                                   replay != nullptr ? replay : ""),
                               config);
  if (!result) {
    LogError("Coin Rush failed [{}]: {}", app::ToString(result.error().error),
             result.error().cause.message);
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
