// The kinematic character controller, over aether's 3D shape casts.
//
// Graduated from examples/lab/controller_lab, which answered whether this shape
// works at all: eight torture cases (rest, step below/above the limit, wall,
// inside corner, ceiling, ledge, varying-normal ground) pass as PROPERTIES.
//
// KINEMATIC: no forces, no restitution, no solver. The E5 dynamics gate was
// evaluated 2026-08-05 and does not open; this needs none of it.
#pragma once

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "aether/scene_core/collision3.hpp"
#include "lantern/features/climb/tuning.hpp"

namespace aether::scene {
class Scene;
}

namespace lantern::climb {

using aether::F32;
using aether::Vec2;
using aether::Vec3;

class Walker {
 public:
  void Reset(Vec3 at);
  void Jump();

  // One fixed step. Horizontal first with sliding, then vertical, then a ground
  // probe — resolving gravity before the move would drop a walker through the
  // gap it was about to step over.
  void Step(aether::scene::Scene& scene, Vec2 input, F32 dt);

  [[nodiscard]] Vec3 Position() const { return position_; }
  [[nodiscard]] Vec3 Facing() const { return facing_; }
  [[nodiscard]] bool Grounded() const { return grounded_; }
  [[nodiscard]] F32 Speed() const { return speed_; }
  [[nodiscard]] Tuning& Config() { return cfg_; }
  [[nodiscard]] const Tuning& Config() const { return cfg_; }

 private:
  void MoveHorizontal(aether::scene::Scene& scene, Vec3 delta);
  [[nodiscard]] bool TryStep(aether::scene::Scene& scene, Vec3 delta);
  void MoveVertical(aether::scene::Scene& scene, F32 dy);
  void ProbeGround(aether::scene::Scene& scene);
  [[nodiscard]] aether::scene::ShapeCastResult Sweep(
      aether::scene::Scene& scene, Vec3 delta) const;

  // THE ONE THING THE QUERY LAYER DOES NOT DO FOR YOU. A sphere cast that
  // starts in contact reports that contact at t=0 for ANY direction, including
  // straight away from it. Unfiltered, a jump dies on its first frame (the
  // floor you stand on is "hit" 0.1 units into the rise) and a walker that
  // starts inside geometry freezes entirely — it can neither move nor fall.
  // Only contacts facing INTO the motion can block it.
  [[nodiscard]] static const aether::scene::ShapeContact* Blocking(
      const aether::scene::ShapeCastResult& hit, Vec3 delta);

  Tuning cfg_;
  Vec3 position_{0.0f, 0.0f, 0.0f};
  Vec3 velocity_{0.0f, 0.0f, 0.0f};
  Vec3 facing_{0.0f, 0.0f, -1.0f};
  F32 speed_ = 0.0f;
  bool grounded_ = false;
};

}  // namespace lantern::climb
