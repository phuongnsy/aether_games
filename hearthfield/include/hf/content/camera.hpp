// Every camera knob, in one declarative place — the board camera's tuning.
//
// In `content/` because that is where this game's tunables live (games/
// AGENTS.md: "Content is data, not code"), and in ONE struct rather than
// scattered constants so the whole feel can be read, diffed and overridden at a
// glance. A dev panel or a world-file override attaches here and nowhere else.
//
// EVERY SMOOTHING VALUE IS A TIME CONSTANT IN SECONDS, used as
// `1 - exp(-dt/tau)`. That is the one damping model in this engine
// (FollowComponent, OrbitComponent::stiffness, app::FlyInput), it is
// frame-rate independent, and it makes each knob read as "how long to catch
// up" instead of as an opaque coefficient. Smaller = tighter.
#pragma once

#include <algorithm>
#include <optional>

#include "aether/core/types.hpp"
#include "hf/content/farm.hpp"

namespace hearthfield::content {

// The island's extent USED TO BE FOUR CONSTANTS HERE, admitting in a comment
// that they were copied from models/hub_island.glb and that changing one left
// the other silently wrong. Sky world s3 moved them into content::kIslands,
// where they are one island's data among several rather than the world's, and
// islands_test now checks them against tools/model_contract.json — which
// records the same mesh's size and is gated by `models-check`.

struct CameraTuning {
  // --- Pan ------------------------------------------------------------------
  // Keys move this many HALF-HEIGHTS per second, not metres. Metres would crawl
  // when zoomed out and race when zoomed in; a fraction of the visible height
  // crosses the same fraction of the SCREEN at every zoom, which is what "pan
  // speed consistent relative to the visible world" means.
  aether::F32 key_pan_per_second = 1.5f;
  // Keys and inertia.
  aether::F32 pan_smoothing = 0.08f;
  // THE DRAG'S OWN STIFFNESS, and the one knob most worth playing with. 0 is
  // perfectly rigid — the board is welded to the cursor, and every jitter in
  // the pointer's per-frame delta goes straight into the view. Small values
  // filter that jitter while still reading as attached; past about 0.10 the
  // world starts sliding behind the finger, which is the "slippery" end.
  aether::F32 drag_smoothing = 0.045f;
  // How long a flick keeps coasting after the finger lifts. Short on purpose:
  // this is a board you are reading, not a map you are throwing.
  aether::F32 inertia_seconds = 0.16f;
  // Ceiling on the coast, in world units/second. Without it a fast flick on a
  // high-refresh screen launches the board off its bounds in one frame.
  aether::F32 max_inertia_speed = 30.0f;

  // --- Zoom -----------------------------------------------------------------
  // The visible half-height, in world units. The board is `columns * cell`
  // across, so the maximum is sized to frame a large farm with air around it
  // and the minimum to put a couple of plots on screen.
  //
  // The maximum is NOT free: zoom out far enough and the visible ground runs
  // off the field into the void. The rig clamps for that on its own (it insets
  // the focus by what is actually visible), but a maximum past the point where
  // the view exceeds the field turns every further notch into "the same board,
  // pinned to the centre" — motion with nothing to show for it.
  aether::F32 min_half_height = 3.0f;
  // 45 since 2026-08-26, up from 18. The old ceiling existed because zooming
  // past it ran the view off the 120 m slab into void; on a floating island
  // that is the shot, and 45 (a 90 m view for a 75 m island) frames the whole
  // thing with sky around it.
  aether::F32 max_half_height = 45.0f;
  // GEOMETRIC per notch, so every notch changes the view by the same
  // proportion. Linear steps feel fast when zoomed in and useless when out.
  aether::F32 zoom_per_notch = 1.15f;
  aether::F32 zoom_smoothing = 0.10f;
  // A wheel is one notch; a trackpad fires a dozen tiny ones per frame and
  // emscripten rounds each to at least ±1 (the trap `app::OrbitInput` records).
  aether::F32 max_notches_per_frame = 3.0f;

  // --- Rotation -------------------------------------------------------------
  // Quarter turns only — four azimuths is an art budget, not a style
  // (2026-08-23-hearthfield-h1-board.md §(d)), and GEA §17.2.2 says this genre
  // does not put yaw under direct player control at all. What is tuned here is
  // how the turn ANIMATES, never where it can stop.
  aether::F32 rotate_smoothing = 0.22f;
  // How far two fingers must twist to buy one quarter turn. Generous, because a
  // twist is easy to produce by accident while pinching.
  aether::F32 twist_per_detent = 0.6f;  // radians

  // --- Gestures -------------------------------------------------------------
  // Past this, a press is a pan and is no longer a tap. Generous for the same
  // reason the editor's is: a finger always slides a little, and a selection
  // that needs a perfectly still hand is worse than one that occasionally
  // fires after a nudge.
  aether::F32 drag_slop_pixels = 8.0f;

  // --- Framing --------------------------------------------------------------
  // Pitch and distance are NOT here: both are authored in farm.world.json and
  // stay exactly as the art placed them. Listing them as tunables would invite
  // exactly the "camera can flip upside down" failure the detents avoid.
};

// TWO limits, because they answer two different questions.
//
// The ROAM rect bounds where the focus may be centred — how far you may wander
// from the farm before there is nothing worth looking at.
//
// `field_half_extent` bounds what may be SEEN: the rig keeps the whole visible
// quad inside it by insetting the roam rect by however much ground is actually
// on screen — which depends on the zoom AND the quarter turn, so it cannot be
// baked into a constant.
//
// It is OPTIONAL, and empty is what the sky world uses. On the 120 m slab it
// was mandatory because past the slab's edge was void and the player would be
// looking at the seam of the world. A floating island inverts that exactly: the
// edge is the subject, so the view is MEANT to leave it and only the focus is
// bounded. Empty means "there is no field the view must stay on" — which
// camera_test used to fake with a half-extent of 100000.
struct CameraBounds {
  aether::F32 min_x = -1.0f;
  aether::F32 max_x = 1.0f;
  aether::F32 min_z = -1.0f;
  aether::F32 max_z = 1.0f;
  std::optional<aether::F32> field_half_extent;

  [[nodiscard]] constexpr aether::F32 ClampX(aether::F32 x) const {
    return std::clamp(x, std::min(min_x, max_x), std::max(min_x, max_x));
  }
  [[nodiscard]] constexpr aether::F32 ClampZ(aether::F32 z) const {
    return std::clamp(z, std::min(min_z, max_z), std::max(min_z, max_z));
  }

  // The middle of the box, which is where a camera with no history belongs.
  // Since s5 a box is centred on ITS island rather than on the origin, so
  // "start at 0,0 and clamp" is the hub's answer and no one else's.
  [[nodiscard]] constexpr aether::Vec2 Centre() const {
    return aether::Vec2{(min_x + max_x) * 0.5f, (min_z + max_z) * 0.5f};
  }
};

// The board is square and centred on the origin (runtime::Grid), so its bounds
// follow from the grid rather than being authored a second time and drifting.
// `margin` lets the focus leave the tiles far enough to look at the fence line
// and the buildings standing beside it.
[[nodiscard]] constexpr CameraBounds BoundsForGrid(
    aether::U32 columns, aether::F32 cell, aether::F32 margin = 4.0f,
    std::optional<aether::F32> field_half_extent = std::nullopt) {
  const auto half = 0.5f * static_cast<aether::F32>(columns) * cell + margin;
  return CameraBounds{.min_x = -half,
                      .max_x = half,
                      .min_z = -half,
                      .max_z = half,
                      .field_half_extent = field_half_extent};
}

// What the sky world ships is `content::Island::Roam()`
// (hf/content/islands.hpp) — the focus roams the ISLAND, not the farm, because
// the farm is one zone of it and the rest is worth looking at. It lives with
// the island rather than here because there is more than one island now, and a
// free function over globals cannot say which one it means.

}  // namespace hearthfield::content
