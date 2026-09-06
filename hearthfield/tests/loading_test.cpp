// The loading screen's geometry. It is the one surface no capture oracle can
// ever reach — a measuring run DRAINS the load before frame 0 by design
// (docs/plans/2026-08-29-loading-screen.md), so a loading frame is never
// presented in any run a digest is taken from. That makes this file the only
// thing that looks at it.
#include "hf/view/loading.hpp"

#include <doctest/doctest.h>

#include <algorithm>

#include "aether/ui/context.hpp"
#include "hf/view/palette.hpp"

using namespace aether;
namespace view = hearthfield::view;

namespace {

constexpr Size kFb{.width = 1280, .height = 720};
constexpr F32 kRef = 720.0f;

// One frame of the loading screen, returned as its quads.
RenderFrame Draw(F32 fraction, std::string_view label = "the island") {
  ui::Context ui;
  ui.BeginFrame(Vec2{-1.0f, -1.0f}, /*pressed=*/false, kFb, nullptr,
                TextureHandle{}, kRef);
  view::DrawLoading(ui, fraction, label);
  return ui.EndFrame();
}

bool Same(Vec4 a, Vec4 b) {
  return std::abs(a.x - b.x) < 0.001f && std::abs(a.y - b.y) < 0.001f &&
         std::abs(a.z - b.z) < 0.001f && std::abs(a.w - b.w) < 0.001f;
}

// Widest quad of `color`, or 0 if there is none. Read off the frame rather than
// recomputed from the constants, so a change to either has to move a pixel.
F32 WidthOf(const RenderFrame& frame, Vec4 color) {
  F32 widest = 0.0f;
  for (const auto& item : frame.items) {
    if (Same(item.color, color)) {
      widest = std::max(widest, item.size.x);
    }
  }
  return widest;
}

// How full the bar reads, 0..1.
F32 Filled(const RenderFrame& frame) {
  const F32 bar = WidthOf(frame, view::kBarEmpty);
  REQUIRE(bar > 0.0f);
  return WidthOf(frame, view::kBarOk) / bar;
}

}  // namespace

TEST_CASE("the loading screen covers the whole canvas") {
  // FULL-BLEED IS THE POINT: no pipeline pass runs on a loading frame, so
  // anything this does not paint is whatever the backbuffer already held.
  const RenderFrame frame = Draw(0.5f);
  REQUIRE_FALSE(frame.items.empty());
  F32 area = 0.0f;
  for (const auto& item : frame.items) {
    area = std::max(area, item.size.x * item.size.y);
  }
  const F32 canvas_w =
      kRef * static_cast<F32>(kFb.width) / static_cast<F32>(kFb.height);
  CHECK(area >= canvas_w * kRef);
}

TEST_CASE("the bar's fill tracks the fraction") {
  CHECK(Filled(Draw(0.5f)) == doctest::Approx(0.5f).epsilon(0.02));
  CHECK(Filled(Draw(0.25f)) == doctest::Approx(0.25f).epsilon(0.02));
  // MONOTONIC across the whole range, which is what a player actually reads —
  // a bar that goes backwards between two phases is worse than no bar.
  F32 previous = -1.0f;
  for (int i = 0; i <= 6; ++i) {
    const F32 filled = Filled(Draw(static_cast<F32>(i) / 6.0f));
    CHECK(filled >= previous);
    previous = filled;
  }
  CHECK(previous == doctest::Approx(1.0f).epsilon(0.01));
}

TEST_CASE("an out-of-range fraction is clamped, not drawn") {
  // The caller is the loop, and a game reporting 1.4 should not paint a bar
  // running off the side of the panel it sits in.
  CHECK(Filled(Draw(4.0f)) <= 1.0f);
  // Below zero draws no fill at all rather than a negative-width quad.
  CHECK(WidthOf(Draw(-1.0f), view::kBarOk) == 0.0f);
}

TEST_CASE("the text scale is restored") {
  // The context is shared with the HUD, and SetTextScale is global state on it:
  // leaving it at the title's scale would draw the next frame's HUD at 2x.
  ui::Context ui;
  ui.BeginFrame(Vec2{-1.0f, -1.0f}, /*pressed=*/false, kFb, nullptr,
                TextureHandle{}, kRef);
  ui.SetTextScale(1.25f);
  view::DrawLoading(ui, 0.5f, "the farm");
  CHECK(ui.GetTextScale() == doctest::Approx(1.25f));
}
