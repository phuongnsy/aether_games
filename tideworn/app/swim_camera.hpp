// SwimCamera — the diving loop's camera pair over one diver (GEA §17.2.2:
// the third-person view is a FOLLOW camera — a look-at on the avatar whose
// motion lags the player; F drops to the first-person camera at the eyes).
//
// THE DRIVER IS NO LONGER HERE. Look, WASD, rise/sink, boost and the eased
// velocity moved into `aether::app::FlyInput` (2026-08-24) — this was the only
// free camera in the repo and the editor wanted the same thing, which is what
// promoted it. What stays is everything that makes it a DIVER rather than a
// flying eye: the water slab, the body's pose and settle-tilt, and the
// third/first-person rig. GEA calls those a different kind of camera, which is
// where the split falls.
#pragma once

#include <algorithm>
#include <cmath>

#include "aether/app/fly_input.hpp"
#include "aether/core/math/quat.hpp"
#include "aether/core/math/transform.hpp"
#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "aether/input/input_snapshot.hpp"
#include "aether/ui/context.hpp"

namespace tideworn::app {

class SwimCamera {
 public:
  // Behind, above and off the right shoulder: a prone body seen dead-astern
  // foreshortens into an unreadable column — the diagonal keeps it a diver.
  aether::F32 follow_distance = 3.6f;
  aether::F32 follow_height = 1.7f;
  aether::F32 follow_side = 0.8f;
  bool third_person = true;

  SwimCamera() {
    fly_.radians_per_pixel = 0.004f;
    fly_.speed = 2.0f;  // m/s; LeftShift doubles it
    // Water drag. The body's settle-tilt eases on the same constant, which is
    // why it is read back out below rather than hardcoded twice.
    fly_.ease_seconds = 0.3f;
    // A diver, not a drone: barely above the surface, never below any fish.
    fly_.min_y = -28.0f;
    fly_.max_y = 0.5f;
  }

  // Take over at a world pose, look angles derived from `forward` so the
  // handover from the orbit camera does not snap.
  void Enter(aether::Vec3 position, aether::Vec3 forward) {
    fly_.Enter(position, forward);
    cam_seeded_ = false;
    tilt_ = kIdleTilt;
  }

  // Real frame dt (presentation, not the sim clock). `glide` adds a steady
  // drive along the look direction — the capture autopilot's stroke.
  // Returns the CAMERA transform; BodyTransform() is the diver's, valid
  // after this call.
  [[nodiscard]] aether::Transform Apply(
      const aether::input::InputSnapshot& input, const aether::ui::Context& ui,
      aether::F32 dt, aether::F32 glide) {
    fly_.forward_drive = glide;
    fly_.Apply(input, ui, dt);

    const aether::Vec3 position = fly_.Position();
    const aether::Vec3 forward = fly_.Forward();
    const aether::F32 yaw = fly_.Yaw();
    const aether::Vec3 right{std::cos(yaw), 0.0f, -std::sin(yaw)};
    // The same ease the driver applies to velocity, on the same constant: a
    // body that settled faster than it decelerated would read as a flinch.
    const aether::F32 blend =
        1.0f - std::exp(-std::max(dt, 0.0f) / fly_.ease_seconds);

    // The BODY: prone along the look when swimming, drifting upright at
    // rest, easing between so a stop reads as settling, not snapping.
    const aether::F32 want_tilt =
        Moving() ? -(1.5707963f + fly_.Pitch()) : kIdleTilt;
    tilt_ = tilt_ + (want_tilt - tilt_) * blend;
    const aether::Quat body_rot =
        QuatFromAxisAngle(aether::Vec3{0.0f, 1.0f, 0.0f}, yaw) *
        QuatFromAxisAngle(aether::Vec3{1.0f, 0.0f, 0.0f}, tilt_);
    // The model's origin is at its FEET (the standing convention); pull the
    // hips onto the diver point so the body pivots about its middle.
    body_ = aether::Transform{
        .position =
            position - Rotate(body_rot, aether::Vec3{0.0f, 0.95f, 0.0f}),
        .rotation = body_rot};

    if (!third_person) {
      // First person: at the eyes, body parked far out of every frustum.
      body_.position.y = -1000.0f;
      return aether::Transform{.position = position + forward * 0.35f,
                               .rotation = LookRotation(forward)};
    }
    const aether::Vec3 want_cam = position - forward * follow_distance +
                                  right * follow_side +
                                  aether::Vec3{0.0f, follow_height, 0.0f};
    // The follow camera LAGS the diver (GEA §17.2.2) — position eases, but
    // it always looks slightly past the avatar so the world leads the body.
    cam_pos_ =
        cam_seeded_ ? cam_pos_ + (want_cam - cam_pos_) * blend : want_cam;
    cam_seeded_ = true;
    return aether::Transform{
        .position = cam_pos_,
        .rotation =
            LookRotation(Normalize(position + forward * 0.8f - cam_pos_))};
  }

  [[nodiscard]] aether::Vec3 Position() const { return fly_.Position(); }
  [[nodiscard]] const aether::Transform& BodyTransform() const { return body_; }
  [[nodiscard]] bool Moving() const { return fly_.Moving(); }

 private:
  static constexpr aether::F32 kIdleTilt = -0.15f;  // slight lean at rest

  // Looks along an ARBITRARY vector, which is why this is not FlyInput's
  // `Rotation()`: the follow camera aims past the diver, not along the look.
  [[nodiscard]] static aether::Quat LookRotation(aether::Vec3 forward) {
    const aether::F32 pitch = std::asin(std::clamp(-forward.y, -1.0f, 1.0f));
    const aether::F32 yaw = std::atan2(-forward.x, -forward.z);
    return QuatFromAxisAngle(aether::Vec3{0.0f, 1.0f, 0.0f}, yaw) *
           QuatFromAxisAngle(aether::Vec3{1.0f, 0.0f, 0.0f}, -pitch);
  }

  aether::app::FlyInput fly_;
  aether::Transform body_;
  aether::Vec3 cam_pos_;
  aether::F32 tilt_ = kIdleTilt;
  bool cam_seeded_ = false;
};

}  // namespace tideworn::app
