// Coin Rush's Android entry point. Selected by CMake for the shared-library
// target, so `main.cpp` keeps the desktop path with no #ifdef between them.
#include <memory>
#include <string>

#include "aether/app/android_host.hpp"
#include "aether/core/config.hpp"
#include "aether/core/log.hpp"
#include "coin_rush.hpp"

using namespace aether;
using namespace game;

// The NDK's native_app_glue calls this on its own thread; returning ends the
// activity. There is no argc/argv here, so the dev command line has no Android
// equivalent — which costs nothing, since `--frames` and friends are dev-only
// and this configuration compiles them out anyway.
extern "C" void android_main(struct android_app* app) {
  // Nothing baked at build time is reachable on a device, so BOTH roots come
  // from where the APK's assets were just unpacked — the compile-time
  // AETHER_ASSET_DIR/AETHER_SHADER_DIR are the build machine's paths.
  const auto staged = app::StageAndroidAssets(app);
  if (!staged) {
    LogError("Coin Rush: cannot stage assets: {}", staged.error().message);
    return;
  }
  const std::string assets = *staged + "/assets";
  const std::string shaders = *staged + "/shaders";

  Config config = app::LoadAppConfig(assets);
  config.Set("asset_dir", assets);

  const auto result = app::RunAndroid(
      app, std::make_unique<CoinRushGame>(0, "", "", shaders), config);
  if (!result) {
    LogError("Coin Rush failed [{}]: {}", app::ToString(result.error().error),
             result.error().cause.message);
  }
}
