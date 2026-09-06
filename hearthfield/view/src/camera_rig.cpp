#include "hf/view/camera_rig.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace hearthfield::view {
namespace {

using namespace aether;

constexpr F32 kQuarterTurn = 1.57079633f;

// THE one damping model in this engine: FollowComponent, OrbitComponent's
// stiffness and app::FlyInput all use it. Frame-rate independent, so two
// half-steps land where one full step does — which is the difference between a
// camera that feels the same on every machine and one that does not.
[[nodiscard]] F32 Blend(F32 dt, F32 tau) {
  if (tau <= 0.0f) {
    return 1.0f;  // a zero time constant means "no smoothing", not "never move"
  }
  return 1.0f - std::exp(-std::max(dt, 0.0f) / tau);
}

// The board's ground basis for a given yaw. Matches OrbitComponent's placement
// exactly (it offsets the camera by +sin/+cos of yaw and looks back), so
// dragging right moves the world right whatever quarter turn the board is on.
struct GroundBasis {
  Vec2 right;
  Vec2 forward;
};

[[nodiscard]] GroundBasis BasisFor(F32 yaw) {
  const F32 s = std::sin(yaw);
  const F32 c = std::cos(yaw);
  return GroundBasis{.right = Vec2{c, -s}, .forward = Vec2{-s, -c}};
}

}  // namespace

std::array<Vec2, 4> VisibleGroundQuad(Vec2 focus, F32 half_height, F32 yaw,
                                      F32 aspect, F32 pitch) {
  const GroundBasis basis = BasisFor(yaw);
  const F32 across = half_height * std::max(aspect, 0.0f);
  // THE FORESHORTENING. A tilt of `pitch` spreads a screen height of
  // `half_height` over `half_height / sin(pitch)` of ground, so at 35 degrees
  // the field runs 1.74x further up the screen than across it. Guarded because
  // a pitch of zero is a camera lying on the ground, which sees to infinity.
  const F32 along = std::sin(pitch) > 0.01f ? half_height / std::sin(pitch)
                                            : half_height * 100.0f;
  std::array<Vec2, 4> quad{};
  int i = 0;
  for (const F32 sx : {-1.0f, 1.0f}) {
    for (const F32 sz : {-1.0f, 1.0f}) {
      quad[static_cast<Usize>(i++)] = Vec2{
          focus.x + basis.right.x * sx * across + basis.forward.x * sz * along,
          focus.y + basis.right.y * sx * across + basis.forward.y * sz * along};
    }
  }
  return quad;
}

F32 FitHalfHeight(F32 authored, Size render_size) {
  // 16:9, the shape the framing was authored and judged against.
  constexpr F32 kReferenceAspect = 16.0f / 9.0f;
  if (render_size.width == 0 || render_size.height == 0) {
    return authored;
  }
  const F32 aspect = static_cast<F32>(render_size.width) /
                     static_cast<F32>(render_size.height);
  // WIDER THAN THE REFERENCE CHANGES NOTHING. An ultrawide already shows more
  // ground than the framing asked for, and shrinking the half-height to claw
  // that back would zoom a 21:9 monitor IN — solving a problem nobody has by
  // creating one.
  return authored * std::max(1.0f, kReferenceAspect / aspect);
}

void CameraRig::Configure(const content::CameraTuning& tuning,
                          const content::CameraBounds& bounds, F32 authored_yaw,
                          F32 half_height, F32 pitch) {
  tuning_ = tuning;
  bounds_ = bounds;
  pitch_ = pitch;
  base_yaw_ = authored_yaw;
  yaw_ = authored_yaw;
  desired_yaw_ = authored_yaw;
  half_height_ =
      std::clamp(half_height, tuning_.min_half_height, tuning_.max_half_height);
  desired_half_height_ = half_height_;
  // START IN THE MIDDLE OF THE BOUNDS, not wherever the old focus clamps to.
  //
  // This clamped a default (0, 0) until 2026-08-29, which framed the HUB
  // perfectly and every satellite wrongly: the hub's roam box contains the
  // origin, near isle's runs from about x = 60 to 120, so the clamp landed the
  // camera on the box's NEAR EDGE with the farm in the corner behind a
  // neighbour's cliff. Travel never showed it because the flight moves the
  // focus before arrival — but `Configure` also runs for a save left on a
  // satellite, so reopening the game there was framed on the edge.
  //
  // `SetBounds` still clamps, and the asymmetry is the point: there the focus
  // is the player's and worth keeping, here there is no focus yet.
  focus_ = desired_focus_ = bounds_.Centre();
  ClampFocus(1.0f);
}

void CameraRig::ApplyRotation(const CameraIntent& intent, F32 dt) {
  // --- Rotation. Whole detents only, eased: the ART BUDGET is four azimuths
  // (h1 §(d)), so what is smoothed is the journey and never the destination.
  detent_ += intent.rotate_steps;
  desired_yaw_ = base_yaw_ + static_cast<F32>(detent_) * kQuarterTurn;
  yaw_ += (desired_yaw_ - yaw_) * Blend(dt, tuning_.rotate_smoothing);
}

F32 CameraRig::ApplyZoom(const CameraIntent& intent, Vec2 anchor_world,
                         bool has_anchor, F32 dt) {
  // --- Zoom. Geometric in the notch, so every notch is the same proportion of
  // the view; clamped BEFORE the ease so the ease has a reachable target and
  // does not spend frames converging on a value it will never be allowed.
  if (intent.zoom_notches != 0.0f) {
    desired_half_height_ =
        std::clamp(desired_half_height_ *
                       std::pow(tuning_.zoom_per_notch, -intent.zoom_notches),
                   tuning_.min_half_height, tuning_.max_half_height);
    if (has_anchor && half_height_ > 0.0f) {
      // Store the anchor as a RATIO of the half-height. Then holding the point
      // still through the whole ease is one multiply per frame, exact for the
      // affine ortho mapping and needing no basis, aspect or pitch term.
      anchor_world_ = anchor_world;
      anchor_ratio_ = Vec2{(anchor_world.x - focus_.x) / half_height_,
                           (anchor_world.y - focus_.y) / half_height_};
      anchored_ = true;
    }
  }
  const F32 before = half_height_;
  half_height_ +=
      (desired_half_height_ - half_height_) * Blend(dt, tuning_.zoom_smoothing);
  return before;
}

void CameraRig::ApplyPan(const CameraIntent& intent, Vec2 viewport, F32 dt) {
  const GroundBasis basis = BasisFor(yaw_);
  // --- Pan. Pixels become world units through the CURRENT zoom, so a drag is
  // 1:1 with the cursor at every zoom level without a speed constant.
  const F32 world_per_pixel =
      viewport.y > 0.0f ? 2.0f * half_height_ / viewport.y : 0.0f;
  Vec2 moved{};
  if (intent.direct &&
      (intent.pan_pixels.x != 0.0f || intent.pan_pixels.y != 0.0f)) {
    // Drag right and the WORLD goes right, so the focus goes left — the board
    // follows the finger, which is the only mapping that feels attached.
    const F32 x = -intent.pan_pixels.x * world_per_pixel;
    const F32 y = -intent.pan_pixels.y * world_per_pixel;
    moved = Vec2{basis.right.x * x + basis.forward.x * -y,
                 basis.right.y * x + basis.forward.y * -y};
    // The DESIRED focus tracks the cursor exactly; the current one chases it on
    // `drag_smoothing`. At 0 that is the rigid 1:1 the plan's D1 argued for; at
    // a few tens of milliseconds it filters the jitter in a pointer's per-frame
    // delta without the world sliding behind the finger. One knob, both feels.
    desired_focus_ =
        Vec2{desired_focus_.x + moved.x, desired_focus_.y + moved.y};
    const F32 grip = Blend(dt, tuning_.drag_smoothing);
    focus_ = Vec2{focus_.x + (desired_focus_.x - focus_.x) * grip,
                  focus_.y + (desired_focus_.y - focus_.y) * grip};
    anchored_ = false;  // the player took over; stop holding the zoom point
    // Velocity for the coast, in world units per second.
    if (dt > 0.0f) {
      velocity_ = Vec2{moved.x / dt, moved.y / dt};
    }
  } else if (intent.pan_pixels.x != 0.0f || intent.pan_pixels.y != 0.0f) {
    // Keys: HALF-HEIGHTS per second, so the board crosses the same fraction of
    // the screen per second at every zoom (plan §3 D3).
    const F32 rate = tuning_.key_pan_per_second * half_height_ * dt;
    const F32 x = intent.pan_pixels.x * rate;
    const F32 y = intent.pan_pixels.y * rate;
    desired_focus_ =
        Vec2{desired_focus_.x + basis.right.x * x + basis.forward.x * -y,
             desired_focus_.y + basis.right.y * x + basis.forward.y * -y};
    velocity_ = {};
    anchored_ = false;
  }

  if (intent.released) {
    // Cap the coast. A flick on a high-refresh screen otherwise produces a
    // one-frame velocity large enough to cross the whole board.
    const F32 speed =
        std::sqrt(velocity_.x * velocity_.x + velocity_.y * velocity_.y);
    if (speed > tuning_.max_inertia_speed && speed > 0.0f) {
      const F32 scale = tuning_.max_inertia_speed / speed;
      velocity_ = Vec2{velocity_.x * scale, velocity_.y * scale};
    }
  } else if (!intent.direct) {
    velocity_ = {};
  }

  // --- Coast, then catch up. Inertia moves the DESIRED focus and lets the same
  // smoother carry the current one, so a release blends into the glide instead
  // of stepping into it.
  if (!intent.direct && (velocity_.x != 0.0f || velocity_.y != 0.0f)) {
    desired_focus_ = Vec2{desired_focus_.x + velocity_.x * dt,
                          desired_focus_.y + velocity_.y * dt};
    const F32 decay = std::exp(-std::max(dt, 0.0f) / tuning_.inertia_seconds);
    velocity_ = Vec2{velocity_.x * decay, velocity_.y * decay};
    // Below a twentieth of a unit per second the glide is invisible and only
    // keeps the camera marked as moving. Stop it dead.
    if (std::abs(velocity_.x) < 0.05f && std::abs(velocity_.y) < 0.05f) {
      velocity_ = {};
    }
  }

  const F32 catch_up = Blend(dt, tuning_.pan_smoothing);
  focus_ = Vec2{focus_.x + (desired_focus_.x - focus_.x) * catch_up,
                focus_.y + (desired_focus_.y - focus_.y) * catch_up};
}

void CameraRig::Apply(const CameraIntent& intent, Vec2 anchor_world,
                      bool has_anchor, Vec2 viewport, F32 dt) {
  const F32 aspect = viewport.y > 0.0f ? viewport.x / viewport.y : 1.0f;

  // In this order and no other: rotation decides the ground basis, zoom decides
  // the world-per-pixel, and pan needs both.
  ApplyRotation(intent, dt);
  const F32 before = ApplyZoom(intent, anchor_world, has_anchor, dt);
  ApplyPan(intent, viewport, dt);

  // --- Hold the zoom anchor, every frame of the ease. Applied AFTER the pan so
  // a player who pans mid-zoom wins, and skipped entirely once the ease has
  // converged so it cannot fight the bounds clamp forever.
  if (anchored_) {
    if (std::abs(half_height_ - desired_half_height_) < 0.001f ||
        half_height_ == before) {
      anchored_ = false;
    } else {
      focus_ = Vec2{anchor_world_.x - anchor_ratio_.x * half_height_,
                    anchor_world_.y - anchor_ratio_.y * half_height_};
      desired_focus_ = focus_;
    }
  }

  ClampFocus(aspect);
}

void CameraRig::ClampFocus(F32 aspect) {
  // How far the visible quad reaches from the focus, on each world axis. Taken
  // from the QUAD rather than from a formula, so a quarter turn is handled by
  // construction: at 45 degrees the rectangle's corners stick out further than
  // either of its sides, and an axis-aligned guess would let them off the
  // field exactly at the detents the board actually uses.
  const std::array<Vec2, 4> quad =
      VisibleGroundQuad(Vec2{}, half_height_, yaw_, aspect, pitch_);
  F32 reach_x = 0.0f;
  F32 reach_z = 0.0f;
  for (const Vec2& corner : quad) {
    reach_x = std::max(reach_x, std::abs(corner.x));
    reach_z = std::max(reach_z, std::abs(corner.y));
  }
  // Inside the field by the whole reach. When the view is WIDER than the field
  // the limit goes to zero and the focus pins to the centre, which is the best
  // available answer: something has to be off-field, so make it symmetric.
  //
  // NO field means the view is ALLOWED off the world — the sky world's islands,
  // where the edge is the subject — and then only the roam rect applies.
  const F32 extent =
      bounds_.field_half_extent.value_or(std::numeric_limits<F32>::max());
  const F32 limit_x = bounds_.field_half_extent
                          ? std::max(0.0f, extent - reach_x)
                          : std::numeric_limits<F32>::max();
  const F32 limit_z = bounds_.field_half_extent
                          ? std::max(0.0f, extent - reach_z)
                          : std::numeric_limits<F32>::max();

  const auto clamp_both = [&](Vec2 v) {
    return Vec2{std::clamp(bounds_.ClampX(v.x), -limit_x, limit_x),
                std::clamp(bounds_.ClampZ(v.y), -limit_z, limit_z)};
  };
  focus_ = clamp_both(focus_);
  // The DESIRED focus is clamped too, not just the current one: leaving it
  // outside would keep the smoother pulling against the wall, and a release
  // would then spring the board back from a place it never visibly reached.
  desired_focus_ = clamp_both(desired_focus_);
}

void CameraRig::Write(scene::OrbitComponent& orbit,
                      scene::CameraComponent& projection) const {
  // The rig owns every ease, so the component must not add a second one on top
  // of the same value (plan §3 D4).
  orbit.stiffness = 0.0f;
  orbit.yaw = yaw_;
  orbit.pivot = Vec3{focus_.x, 0.0f, focus_.y};
  projection.ortho_half_height = half_height_;
}

}  // namespace hearthfield::view
