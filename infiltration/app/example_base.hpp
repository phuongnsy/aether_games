// The slice's base: the engine dev tools wired in once — the F3 HUD and the
// control panel, default OFF for clean captures.
//
// COPIED FROM examples/example_base.hpp WITH THE MOVE (i1), because the slice
// derives from it. Only the example-hub block is gone, which a standalone game
// could never enter; the four capture digests are what proves the rest is
// unchanged. i2 folds what survives into the view layer.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

#include "aether/app/app.hpp"
#include "aether/app/app_events.hpp"
#include "aether/app/orbit_input.hpp"
#include "aether/core/log.hpp"
#include "aether/core/math/vec.hpp"
#include "aether/input/input_snapshot.hpp"
#include "aether/inspector/inspector.hpp"
#include "aether/platform/window.hpp"
#include "aether/resources/solid_texture.hpp"
#include "aether/resources/texture.hpp"
#include "aether/rhi/device.hpp"
#include "aether/scene_core/orbit_component.hpp"
#include "aether/scene_core/scene.hpp"
#include "aether/ui/context.hpp"
#ifdef AETHER_DEV_TOOLS
#endif

namespace examples {

class ExampleGame : public aether::app::Game {
 protected:
  // The example's control panel, owned here so the base can drive its frame:
  // examples declare properties into it and never touch a UI library.
  aether::inspector::Inspector inspector_;

  // The scene frame every example builds: a render-size viewport plus the F4
  // debug-bounds flag. Centralised so a new example gets both for free — and
  // cannot forget the flag, which is how the examples missed F4 the first time.
  [[nodiscard]] static aether::RenderFrame BuildSceneFrame(
      aether::scene::Scene& scene, const aether::app::AppContext& ctx) {
    return scene.BuildRenderFrame(
        aether::Viewport{.width = ctx.render_size.width,
                         .height = ctx.render_size.height},
        ctx.ShowDebugBounds(), &ctx.jobs);
  }

  // Examples override THIS, not Update: the base claims whatever the dev panel
  // is using first, so an example's own input handling never has to ask. Which
  // is the whole point — the engine does not know the panel exists, and neither
  // does the example.
  virtual void OnUpdate(const aether::app::AppContext& /*ctx*/, aether::F32
                        /*dt*/) {}

  void Update(const aether::app::AppContext& ctx, aether::F32 dt) final {
    // ORDER IS THE WHOLE THING: the panel opens its frame on the RAW snapshot,
    // and only then is what it claimed masked away. Claiming first hands the
    // panel its own masked input, so it never sees the click.
    //
    // OpenFrame answers false when the panel has no dev UI, so this needs no
    // build-configuration knowledge — that is the inspector's business.
    const aether::Size fb = ctx.window.FramebufferSize();
    const aether::Size win = ctx.window.WindowSize();
    if (inspector_.OpenFrame(ctx.device,
                             aether::Vec2{static_cast<aether::F32>(fb.width),
                                          static_cast<aether::F32>(fb.height)},
                             aether::Vec2{static_cast<aether::F32>(win.width),
                                          static_cast<aether::F32>(win.height)},
                             ctx.input, dt)) {
      ctx.input.Suppress(inspector_.WantsMouse(), inspector_.WantsKeyboard());
    }
    OnUpdate(ctx, dt);
  }

  // Draw the control panel — except during a capture (`ctx.Capturing()`), so
  // doc visuals show only the effect. Route every example through this.
  // A shipping build has no dev UI and Submit is simply a no-op.
  void DrawInspector(aether::inspector::Inspector& inspector,
                     const aether::app::AppContext& ctx) {
    if (ctx.Capturing()) {
      return;
    }
    (void)inspector.Submit(ctx.device);
  }

#ifdef AETHER_DEV_TOOLS
 private:
#endif
};

// Promoted to `app` when the editor needed the same bindings; the alias keeps
// every example's `examples::OrbitInput` spelling working.
using OrbitInput = aether::app::OrbitInput;

// Promoted to `resources`; the aliases keep every example's spelling working.
using aether::resources::MakeSolidTexture;
using aether::resources::MakeUiWhite;

}  // namespace examples
