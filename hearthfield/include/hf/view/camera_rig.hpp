// The board camera's MOTION: where the view is centred, how far out it is
// zoomed, which quarter turn it is on — and the smoothing that makes all three
// feel like a city-builder rather than a spreadsheet.
//
// Pure state plus arithmetic: it takes an intent and a dt, and it writes the
// result onto the two components the scene already has. It never reads a
// device, so the whole feel is unit-testable with no window.
//
// GEA §17.2.2's RTS camera — "floats above the terrain, looking down at an
// angle... panned about over the terrain, but the pitch and yaw of the camera
// are usually not under direct player control". PITCH AND DISTANCE ARE NEVER
// TOUCHED HERE; they stay exactly as farm.world.json authored them, which is
// also why the camera cannot clip the ground: it never moves toward it.
#pragma once

#include <array>

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "aether/scene_core/camera_component.hpp"
#include "aether/scene_core/orbit_component.hpp"
#include "hf/content/camera.hpp"
#include "hf/view/camera_input.hpp"

namespace hearthfield::view {

// THE FOUR GROUND CORNERS THE PLAYER CAN SEE, in world XZ.
//
// This is what "off the map" is actually asked against — not an angle. The
// camera's pitch and yaw are both fixed by design, so it can never look under
// the world or flip; the only way to see the seam is for this quad to run past
// the edge of the ground slab.
//
// Under a parallel projection the mapping is affine, so the region is exactly a
// rectangle: `half_height * aspect` across the screen's horizontal, and
// `half_height / sin(pitch)` along the view axis — the ground is FORESHORTENED
// by the tilt, so a screen that is 6 metres tall covers 10.5 metres of field at
// 35 degrees. Forgetting that divisor is how a camera that looks bounded still
// shows the void.
[[nodiscard]] std::array<aether::Vec2, 4> VisibleGroundQuad(
    aether::Vec2 focus, aether::F32 half_height, aether::F32 yaw,
    aether::F32 aspect, aether::F32 pitch);

// THE AUTHORED FRAMING IS A 16:9 NUMBER, and a taller window has to be told.
// `ortho_half_height` is the VERTICAL half-extent, so the horizontal follows
// the aspect: 20 gives 35.6 m across at 16:9 and only 11.25 m at 9:16, against
// a board whose 45-degree diagonal is about 27 m — which is why a portrait
// phone opened on a farm CUT OFF at both sides. This scales the authored value
// to hold the reference horizontal coverage; `Configure` then clamps it to
// `max_half_height`.
//
// IT CANNOT FULLY SUCCEED AND DOES NOT PRETEND TO: holding 35.6 m at 9:16 wants
// a half-height of 63 against a max of 45, so portrait still shows less than
// landscape. What it buys is the whole available range instead of a sixth of
// it. Free function, so the arithmetic is testable without a window.
[[nodiscard]] aether::F32 FitHalfHeight(aether::F32 authored,
                                        aether::Size render_size);

class CameraRig {
 public:
  // `authored_yaw` and `half_height` come from the world file — the rig adopts
  // the framing the art placed rather than imposing one.
  // `pitch` is the authored tilt, in radians. The rig never changes it — it
  // needs it only to know how much ground a screen height covers.
  void Configure(const content::CameraTuning& tuning,
                 const content::CameraBounds& bounds, aether::F32 authored_yaw,
                 aether::F32 half_height, aether::F32 pitch);

  // `anchor_world` is the ground point under the zoom anchor, unprojected by
  // app/ — the only part of this that needs a viewport, and the one thing the
  // rig cannot compute for itself (hf/runtime/grid.hpp says a viewport belongs
  // to app/, and the same holds here).
  // `viewport` is in pixels: its height converts a drag to world units, and its
  // aspect decides how much field is on screen either side of the focus.
  void Apply(const CameraIntent& intent, aether::Vec2 anchor_world,
             bool has_anchor, aether::Vec2 viewport, aether::F32 dt);

  // Push the eased state onto the scene. Separate from Apply so a test can
  // check the motion without building a Scene.
  void Write(aether::scene::OrbitComponent& orbit,
             aether::scene::CameraComponent& projection) const;

  // --- travel (sky world s5) --------------------------------------------
  //
  // Two narrow verbs rather than a second Configure, and the difference is the
  // point: Configure re-adopts the AUTHORED framing, which would reset the
  // player's zoom and quarter turn on arrival — the exact continuity one
  // persistent camera was chosen to keep (ADR-0142).

  // Swap which island's roam rect applies. The focus is not moved, so a caller
  // that changes bounds without also carrying the focus across will find it
  // clamped into the new box on the next Apply — which is what arriving means.
  void SetBounds(const content::CameraBounds& bounds) { bounds_ = bounds; }

  // Put the focus somewhere, damping and inertia included. BOTH the current and
  // the desired value, and the velocity cleared: a flight is not a drag, and
  // leaving either behind would have the camera spring back toward the island
  // it just left the moment the player touches the screen.
  void SnapFocus(aether::Vec2 focus) {
    focus_ = focus;
    desired_focus_ = focus;
    velocity_ = aether::Vec2{};
    anchored_ = false;
  }

  [[nodiscard]] aether::Vec2 Focus() const { return focus_; }
  [[nodiscard]] aether::F32 HalfHeight() const { return half_height_; }
  [[nodiscard]] aether::F32 Yaw() const { return yaw_; }
  [[nodiscard]] aether::Vec2 Velocity() const { return velocity_; }

 private:
  // Apply's stages, split out because each owns a different set of members and
  // they compose in ONE order: rotation feeds the ground basis, zoom feeds the
  // world-per-pixel, and pan needs both. Left as methods rather than free
  // functions because every one of them mutates the rig's own state.
  void ApplyRotation(const CameraIntent& intent, aether::F32 dt);
  // Returns the half-height BEFORE the ease: the anchor hold needs it to tell
  // "the ease is still moving" from "it has arrived", and only this function
  // sees both sides of the step.
  [[nodiscard]] aether::F32 ApplyZoom(const CameraIntent& intent,
                                      aether::Vec2 anchor_world,
                                      bool has_anchor, aether::F32 dt);
  // Derives the ground basis from `yaw_` itself: GroundBasis is private to the
  // .cpp, and a header may not name it.
  void ApplyPan(const CameraIntent& intent, aether::Vec2 viewport,
                aether::F32 dt);

  // Clamps to the roam rect AND to whatever keeps the visible quad on the
  // field, whichever is tighter. Zero-argument would be a lie: the inset
  // depends on the zoom and the quarter turn, so it moves every frame.
  void ClampFocus(aether::F32 aspect);

  content::CameraTuning tuning_;
  content::CameraBounds bounds_;

  // DESIRED versus CURRENT, the split `OrbitComponent` documents: the desired
  // value is what input writes, the current value is where the camera actually
  // is this frame, and the gap between them is the smoothing. A direct drag
  // writes BOTH, which is what makes it 1:1 with the finger.
  aether::Vec2 focus_{};
  aether::Vec2 desired_focus_{};
  aether::F32 half_height_ = 12.0f;
  aether::F32 desired_half_height_ = 12.0f;
  aether::F32 yaw_ = 0.0f;
  aether::F32 desired_yaw_ = 0.0f;
  aether::F32 base_yaw_ = 0.0f;
  aether::F32 pitch_ = 0.6108652f;  // 35 degrees, the authored tilt
  int detent_ = 0;

  // Coast after a flick. Held in world units/second so it is independent of
  // both zoom and frame rate.
  aether::Vec2 velocity_{};

  // The zoom anchor, kept as a RATIO of the half-height rather than as a
  // correction applied once. See the plan §3 D2: the ease lasts many frames, so
  // a one-shot correction anchors the first and drifts through the rest.
  aether::Vec2 anchor_world_{};
  aether::Vec2 anchor_ratio_{};
  bool anchored_ = false;
};

}  // namespace hearthfield::view
