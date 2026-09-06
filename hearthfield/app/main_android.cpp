// Hearthfield's Android entry point. Selected by CMake for the shared-library
// target, so `main.cpp` keeps the desktop path with no #ifdef between them.
//
// THIS IS THE FIRST PLACE THE BOARD CAMERA'S TOUCH GESTURES RUN FOR REAL.
// Only the Android backend fills `InputSnapshot::Touches()` — glfw never does —
// so pinch and twist have been proven against synthetic contact spans and
// nothing else (ADR-0116's standing risk).
#include <memory>
#include <string>

#include "aether/app/android_host.hpp"
#include "aether/core/config.hpp"
#include "aether/core/log.hpp"
#include "hearthfield.hpp"
#include "hf/content/farm.hpp"

using namespace aether;

extern "C" void android_main(struct android_app* app) {
  // Nothing baked at build time is reachable on a device, so the asset root
  // comes from where the APK's assets were just unpacked — the compile-time
  // AETHER_ASSET_DIR is the build machine's path.
  const auto staged = app::StageAndroidAssets(app);
  if (!staged) {
    LogError("hearthfield: cannot stage assets: {}", staged.error().message);
    return;
  }
  const std::string assets = *staged + "/assets";

  Config config = app::LoadAppConfig(assets);
  config.Set("asset_dir", assets);

  // A DEFAULT FARM, and no command line to change it: `--grid` and friends are
  // dev options that this configuration compiles out anyway.
  //
  // Saving is asked for and DOES NOT HAPPEN YET. The engine has no user data
  // directory on Android — that needs internal storage through JNI — so the
  // game warns once at load and runs without persisting. Left as `false`
  // rather than hardcoded off, so the farm starts saving the day that lands
  // instead of needing this line found and changed.
  const auto result = app::RunAndroid(
      app,
      std::make_unique<hearthfield::app::HearthfieldGame>(
          hearthfield::content::kDefaultColumns, /*sow=*/false,
          /*autopilot=*/false, /*offline=*/-1, /*no_save=*/false,
          /*screen=*/"", /*rain=*/false),
      config);
  if (!result) {
    LogError("hearthfield failed [{}]: {}", app::ToString(result.error().error),
             result.error().cause.message);
  }
}
