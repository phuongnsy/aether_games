#include "hf/view/camera_gestures.hpp"

#include <cmath>

#include "aether/core/math/scalar.hpp"

namespace hearthfield::view {
namespace {

using namespace aether;

// Angle of b - a, for the twist. `atan2` rather than a dot product because the
// SIGN is the whole point: a dot gives the magnitude of the turn and not which
// way it went.
[[nodiscard]] F32 AngleOf(Vec2 a, Vec2 b) {
  return std::atan2(b.y - a.y, b.x - a.x);
}

// The shortest way round. Two fingers crossing the ±pi seam would otherwise
// report a turn of almost a full circle in one frame, which at any sane
// threshold is several quarter turns of the board.
[[nodiscard]] F32 ShortestAngle(F32 radians) {
  while (radians > kPi) {
    radians -= 2.0f * kPi;
  }
  while (radians < -kPi) {
    radians += 2.0f * kPi;
  }
  return radians;
}

}  // namespace

TouchGesture TouchGestures::Update(std::span<const platform::Touch> touches) {
  Contact live[kTracked];
  Usize live_count = 0;
  for (const platform::Touch& touch : touches) {
    if (!platform::IsLive(touch.phase) || live_count == kTracked) {
      continue;
    }
    live[live_count++] =
        Contact{.id = touch.id, .position = touch.position, .live = true};
  }

  TouchGesture out;
  out.fingers = static_cast<U32>(live_count);
  out.ended = live_count == 0 && count_ > 0;
  out.began = live_count > 0 && count_ == 0;

  if (live_count == 0) {
    count_ = 0;
    travel_ = 0.0f;
    return out;
  }

  out.centroid = live[0].position;
  if (live_count == 2) {
    out.centroid = Vec2{0.5f * (live[0].position.x + live[1].position.x),
                        0.5f * (live[0].position.y + live[1].position.y)};
  }

  // Match by ID, never by index. This is the whole reason contacts are tracked
  // rather than read fresh: lifting one of two fingers renumbers the span, and
  // an index match would read the survivor as having jumped to where the other
  // one was.
  const Contact* previous[kTracked] = {nullptr, nullptr};
  Usize matched = 0;
  for (Usize i = 0; i < live_count; ++i) {
    for (Usize j = 0; j < count_; ++j) {
      if (tracked_[j].id == live[i].id) {
        previous[i] = &tracked_[j];
        ++matched;
        break;
      }
    }
  }

  // A gesture that just changed finger count reports NO motion this frame: the
  // pair it would measure against is not the pair that produced the last
  // sample. One dropped frame of delta is invisible; the spike is not.
  const bool comparable = matched == live_count && live_count == count_;
  if (comparable) {
    if (live_count == 1) {
      out.drag = Vec2{live[0].position.x - previous[0]->position.x,
                      live[0].position.y - previous[0]->position.y};
      travel_ += std::sqrt(out.drag.x * out.drag.x + out.drag.y * out.drag.y);
    } else {
      const Vec2 was_a = previous[0]->position;
      const Vec2 was_b = previous[1]->position;
      const F32 was = std::sqrt((was_b.x - was_a.x) * (was_b.x - was_a.x) +
                                (was_b.y - was_a.y) * (was_b.y - was_a.y));
      const F32 now = std::sqrt((live[1].position.x - live[0].position.x) *
                                    (live[1].position.x - live[0].position.x) +
                                (live[1].position.y - live[0].position.y) *
                                    (live[1].position.y - live[0].position.y));
      // Guard the ratio, not the subtraction: two fingers touching down at the
      // same pixel is rare and a division by it is not recoverable.
      if (was > 1.0f && now > 1.0f) {
        out.pinch = now / was;
      }
      out.twist = ShortestAngle(AngleOf(live[0].position, live[1].position) -
                                AngleOf(was_a, was_b));
      // A two-finger gesture moves the board as well as scaling it, so the
      // centroid's travel is the pan.
      const Vec2 was_centroid{0.5f * (was_a.x + was_b.x),
                              0.5f * (was_a.y + was_b.y)};
      out.drag = Vec2{out.centroid.x - was_centroid.x,
                      out.centroid.y - was_centroid.y};
    }
  }

  for (Usize i = 0; i < live_count; ++i) {
    tracked_[i] = live[i];
  }
  count_ = live_count;
  return out;
}

}  // namespace hearthfield::view
