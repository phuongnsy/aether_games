// One and two finger gestures, from raw contacts.
//
// THIS DOES NOT EXIST IN THE ENGINE. `platform/touch.hpp` reports contacts raw
// and says so in its first line — "actions/gestures belong above, in `input`" —
// but nothing above ever built one, so a pinch had to be written before the
// board could be zoomed with two fingers. It lives in the game because one
// consumer is not a promotion (`app::OrbitInput` records that rule about
// itself); it is written as a pure function over a contact SPAN, holding no
// device and no snapshot, so promoting it into `aether::input` later is a file
// move rather than a rewrite.
//
// Contacts are identified by `Touch::id`, never by position in the span: a
// finger lifting renumbers the span and would otherwise read as both fingers
// teleporting — a pinch spike of a hundred percent in one frame.
#pragma once

#include <span>

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "aether/platform/touch.hpp"

namespace hearthfield::view {

// What the fingers did between the last frame and this one. All deltas are
// zero on the frame a gesture begins — a gesture needs two samples before it
// has moved, and reporting motion from a single frame is how a tap becomes a
// flick.
struct TouchGesture {
  aether::U32 fingers = 0;
  // ONE finger: how far it moved, in framebuffer pixels.
  aether::Vec2 drag{};
  // TWO fingers: how the span between them changed, and how it turned.
  // `pinch` is a RATIO (>1 = fingers spreading = zoom in), so it composes by
  // multiplication and is scale-free; a pixel difference would mean different
  // things at different finger separations.
  aether::F32 pinch = 1.0f;
  aether::F32 twist = 0.0f;  // radians, positive = counter-clockwise
  // Midpoint of the live contacts, in framebuffer pixels: what a two-finger
  // zoom anchors on and what a one-finger drag reports as its position.
  aether::Vec2 centroid{};
  // The gesture began this frame — the press edge, for a latch.
  bool began = false;
  // Every contact lifted this frame. Distinct from `fingers == 0` on a later
  // frame, which is simply nothing happening.
  bool ended = false;
};

// Frame-to-frame state. One instance per gesture surface.
class TouchGestures {
 public:
  // `touches` is this frame's raw contacts, as `InputSnapshot::Touches()`
  // reports them: live contacts plus any that ended this frame.
  [[nodiscard]] TouchGesture Update(
      std::span<const aether::platform::Touch> touches);

  // Total distance the primary contact has travelled since it went down. What
  // the drag-versus-tap decision is made on, because a finger that wanders and
  // returns is still not a tap.
  [[nodiscard]] aether::F32 TravelPixels() const { return travel_; }

 private:
  struct Contact {
    aether::U32 id = 0;
    aether::Vec2 position;
    bool live = false;
  };

  // Two is all this needs: a third finger neither pans nor zooms, and tracking
  // it would only let a stray palm change what the other two mean.
  static constexpr aether::Usize kTracked = 2;

  Contact tracked_[kTracked];
  aether::Usize count_ = 0;
  aether::F32 travel_ = 0.0f;
};

}  // namespace hearthfield::view
