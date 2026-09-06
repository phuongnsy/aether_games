#include "lantern/features/climb/walker.hpp"

#include <algorithm>
#include <cmath>

#include "aether/core/math/geometry.hpp"
#include "aether/scene_core/scene.hpp"

namespace lantern::climb {

using aether::Sphere;
using aether::scene::ShapeCastResult;
using aether::scene::ShapeContact;

void Walker::Reset(Vec3 at) {
  position_ = at;
  velocity_ = Vec3{};
  grounded_ = false;
  speed_ = 0.0f;
}

void Walker::Jump() {
  if (grounded_) {
    velocity_.y = cfg_.jump_speed;
    grounded_ = false;
  }
}

const ShapeContact* Walker::Blocking(const ShapeCastResult& hit, Vec3 delta) {
  const F32 len = Length(delta);
  if (!hit.Hit() || len < 1e-6f) {
    return nullptr;
  }
  const Vec3 dir = delta / len;
  for (const ShapeContact& contact : hit.contacts) {
    if (Dot(contact.normal, dir) < -1e-3f) {
      return &contact;
    }
  }
  return nullptr;
}

ShapeCastResult Walker::Sweep(aether::scene::Scene& scene, Vec3 delta) const {
  return scene.SphereCast(Sphere{.center = position_, .radius = cfg_.radius},
                          delta);
}

void Walker::MoveHorizontal(aether::scene::Scene& scene, Vec3 delta) {
  // Two passes: one to meet a wall, one to slide along it into a corner. A
  // third buys nothing measurable and would hide non-convergence.
  for (int pass = 0; pass < 2 && LengthSquared(delta) > 1e-8f; ++pass) {
    const ShapeCastResult hit = Sweep(scene, delta);
    const ShapeContact* block = Blocking(hit, delta);
    if (block == nullptr) {
      position_ = position_ + delta;
      return;
    }
    const Vec3 moved = delta * std::max(0.0f, hit.t - 1e-3f);
    // Step-up is tried BEFORE sliding: a step and a wall are the same contact
    // to a sphere cast, and only headroom tells them apart.
    if (pass == 0 && TryStep(scene, delta)) {
      return;
    }
    position_ = position_ + moved;
    const Vec3 remaining = delta - moved;
    const Vec3 n = block->normal;
    delta = remaining - (n * Dot(remaining, n));
  }
}

bool Walker::TryStep(aether::scene::Scene& scene, Vec3 delta) {
  const Vec3 start = position_;
  const Vec3 up{0.0f, cfg_.step_up, 0.0f};
  if (Blocking(Sweep(scene, up), up) != nullptr) {
    return false;  // no headroom to rise into
  }
  position_ = position_ + up;
  if (Blocking(Sweep(scene, delta), delta) != nullptr) {
    position_ = start;
    return false;  // blocked up there too: a wall, not a step
  }
  position_ = position_ + delta;

  const Vec3 down{0.0f, -cfg_.step_up, 0.0f};
  const ShapeCastResult land = Sweep(scene, down);
  if (Blocking(land, down) == nullptr) {
    position_ = start;  // nothing to land on: that was a gap, not a step
    return false;
  }
  position_ = position_ + (down * std::max(0.0f, land.t - 1e-3f));
  return true;
}

void Walker::MoveVertical(aether::scene::Scene& scene, F32 dy) {
  if (std::abs(dy) < 1e-6f) {
    return;
  }
  const Vec3 delta{0.0f, dy, 0.0f};
  const ShapeCastResult hit = Sweep(scene, delta);
  if (Blocking(hit, delta) == nullptr) {
    position_ = position_ + delta;
    return;
  }
  position_ = position_ + (delta * std::max(0.0f, hit.t - 1e-3f));
  velocity_.y = 0.0f;  // floor or ceiling; either way the rise or fall ends
}

void Walker::ProbeGround(aether::scene::Scene& scene) {
  // Grounded is decided by a SHORT probe, not by the last collision: a walker
  // that merely ran out of downward velocity mid-air would read as standing.
  const Vec3 down{0.0f, -(cfg_.snap + 1e-3f), 0.0f};
  const ShapeCastResult hit = Sweep(scene, down);
  const ShapeContact* floor = Blocking(hit, down);
  grounded_ = false;
  if (floor == nullptr) {
    return;
  }
  if (floor->normal.y >= cfg_.ground_cos) {
    grounded_ = true;
    if (velocity_.y <= 0.0f) {
      velocity_.y = 0.0f;
      position_.y -= (cfg_.snap + 1e-3f) * hit.t;
    }
  }
}

void Walker::Step(aether::scene::Scene& scene, Vec2 input, F32 dt) {
  const F32 len = Length(input);
  if (len > 1e-3f) {
    const Vec2 dir = input / len;
    facing_ = Vec3{dir.x, 0.0f, dir.y};
  }
  speed_ = std::min(len, 1.0f) * cfg_.walk_speed;

  const Vec3 wish{facing_.x * speed_, 0.0f, facing_.z * speed_};
  MoveHorizontal(scene, wish * dt);

  velocity_.y += cfg_.gravity * dt;
  MoveVertical(scene, velocity_.y * dt);
  ProbeGround(scene);
}

}  // namespace lantern::climb
