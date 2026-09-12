// The space the slice is played in: the geometry, and the same space as a
// QUERY. Both live here because they describe one thing — a level whose
// boxes disagreed with its triangles would be the bug this layer exists to
// make impossible.
//
// A CONTENT layer, in lantern's shape: no scene, no render, no device. It was
// the head of app/infiltration.cpp until 2026-09-12 (i2), and the four capture
// digests are what proves the move changed nothing.
#pragma once

#include <array>
#include <optional>
#include <vector>

#include "aether/core/geometry_query.hpp"
#include "aether/core/math/geometry.hpp"
#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"

namespace infiltration::content {

// The engine names, as the slice has always spelled them: this code was one
// anonymous namespace in a file that opens with the same line.
using namespace aether;  // NOLINT(google-build-using-namespace)

// --- the level ---------------------------------------------------------------
// The same corridor, wall and gap `nav_lab` bakes: a 20 m floor with a 2.5 m
// slab from x = -10 to x = +4, so the only way between the halves is round the
// open end. The geometry is duplicated rather than shared because a lab is a
// probe and a slice is a consumer; they should be able to diverge.

struct Level {
  std::vector<Vec3> vertices;
  std::vector<U32> indices;
};

inline void AddQuad(Level& l, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
  const auto base = static_cast<U32>(l.vertices.size());
  l.vertices.insert(l.vertices.end(), {a, b, c, d});
  for (U32 i : {0u, 2u, 1u, 0u, 3u, 2u}) {
    l.indices.push_back(base + i);
  }
}

constexpr F32 kHalf = 10.0f;
constexpr F32 kWallTop = 2.5f;
constexpr F32 kWallEnd = 4.0f;
constexpr F32 kWallZ = 0.4f;  // half-thickness

// GEA §13.5.3.6's geometry, sized to the controller's own limits so the level
// asks each question once. `max_climb` is 0.4 m below (`Human()` in
// `character.hpp`), so 0.25 m is a kerb and 0.7 m is a wall.
//
// THE KERB IS ON THE ROUTE AND THE RAMP IS NOT, deliberately. The kerb is a
// 0.25 m walkway strip across the only way round the wall's open end, so every
// run crosses it twice and the assertions below are about it. The 0.7 m block
// and its ramp sit in the north-east corner, off the patrol and off both
// autopilot legs — because the guards are CROWD-driven over a navmesh built
// from a flat floor (ADR-0181 owns their movement, not this controller), and
// putting something they cannot climb on their chase path would look broken
// for a reason that has nothing to do with §13.5.3.6.
constexpr F32 kKerbTop = 0.25f;
constexpr F32 kKerbX0 = 4.6f;
constexpr F32 kKerbX1 = 7.4f;
constexpr F32 kBlockTop = 0.7f;  // too tall to step: `max_climb` refuses it
constexpr F32 kBlockZ0 = 6.2f;
constexpr F32 kBlockZ1 = 8.0f;
constexpr F32 kRampRun = 1.5f;  // 0.7 m over 1.5 m is ~25 degrees, inside 45

inline Level MakeLevel() {
  Level l;
  AddQuad(l, Vec3{-kHalf, 0, -kHalf}, Vec3{kHalf, 0, -kHalf},
          Vec3{kHalf, 0, kHalf}, Vec3{-kHalf, 0, kHalf});
  for (const F32 z : {-kWallZ, kWallZ}) {
    AddQuad(l, Vec3{-kHalf, 0, z}, Vec3{kWallEnd, 0, z},
            Vec3{kWallEnd, kWallTop, z}, Vec3{-kHalf, kWallTop, z});
  }
  AddQuad(l, Vec3{-kHalf, kWallTop, -kWallZ}, Vec3{kWallEnd, kWallTop, -kWallZ},
          Vec3{kWallEnd, kWallTop, kWallZ}, Vec3{-kHalf, kWallTop, kWallZ});

  // The kerb: a low walkway strip with a face at each end, which is what makes
  // it a STEP rather than a bump.
  AddQuad(l, Vec3{kKerbX0, kKerbTop, -kHalf}, Vec3{kKerbX1, kKerbTop, -kHalf},
          Vec3{kKerbX1, kKerbTop, kHalf}, Vec3{kKerbX0, kKerbTop, kHalf});
  for (const F32 x : {kKerbX0, kKerbX1}) {
    AddQuad(l, Vec3{x, 0, -kHalf}, Vec3{x, kKerbTop, -kHalf},
            Vec3{x, kKerbTop, kHalf}, Vec3{x, 0, kHalf});
  }

  // The block and its ramp: the same 0.7 m gained two ways, one refused by
  // `max_climb` and one accepted by the slope cutoff.
  AddQuad(
      l, Vec3{kKerbX0, kBlockTop, kBlockZ0}, Vec3{kKerbX1, kBlockTop, kBlockZ0},
      Vec3{kKerbX1, kBlockTop, kBlockZ1}, Vec3{kKerbX0, kBlockTop, kBlockZ1});
  AddQuad(l, Vec3{kKerbX0, 0, kBlockZ0}, Vec3{kKerbX1, 0, kBlockZ0},
          Vec3{kKerbX1, kBlockTop, kBlockZ0},
          Vec3{kKerbX0, kBlockTop, kBlockZ0});
  // Rising WESTWARD onto the block's top, so the two meet: high edge at
  // `kKerbX1`, foot at `kKerbX1 + kRampRun`.
  AddQuad(l, Vec3{kKerbX1, kBlockTop, kBlockZ0},
          Vec3{kKerbX1 + kRampRun, 0, kBlockZ0},
          Vec3{kKerbX1 + kRampRun, 0, kBlockZ1},
          Vec3{kKerbX1, kBlockTop, kBlockZ1});
  return l;
}

// The level as a query, for the four consumers that need to ask about it:
// perception's line of sight, cover, audio propagation, and now the character
// controller. Written here rather than derived from the triangle soup because
// the point is the SEAM — any `core::GeometryQuery3` satisfies all four, and
// `physics::World` would do.
//
// BOXES AND ONE PLANE, RATHER THAN THE HAND-ROLLED Z-SLAB IT WAS. The wall used
// to be two z-faces solved inline, which returned `+z` as the normal for BOTH
// of them; that was invisible to line of sight (which wants a yes/no) and is
// not invisible to a controller, which slides along the normal it is handed.
// `core`'s own `Raycast(ray, AABB)` gives the true face, so the wall is the
// same solid with correct normals.
class WallQuery final : public GeometryQuery3 {
 public:
  WallQuery()
      : boxes_{// THE FLOOR, which this query did not have and now must. The
               // old inline version knew only the wall, because its consumers
               // asked about line of sight and nothing else; a controller asks
               // "what am I standing on", and a query with no ground answers
               // that the player is falling — forever, which is what the first
               // run of this change did.
               AABB{.min = Vec3{-kHalf, -1.0f, -kHalf},
                    .max = Vec3{kHalf, 0.0f, kHalf}},
               // the wall
               AABB{.min = Vec3{-kHalf, 0.0f, -kWallZ},
                    .max = Vec3{kWallEnd, kWallTop, kWallZ}},
               // the kerb, on the route
               AABB{.min = Vec3{kKerbX0, -1.0f, -kHalf},
                    .max = Vec3{kKerbX1, kKerbTop, kHalf}},
               // the block, off it
               AABB{.min = Vec3{kKerbX0, -1.0f, kBlockZ0},
                    .max = Vec3{kKerbX1, kBlockTop, kBlockZ1}}} {}

  [[nodiscard]] std::optional<GeometryHit3> Raycast(const Ray3& ray,
                                                    U32) const override {
    std::optional<GeometryHit3> best;
    for (Usize i = 0; i < boxes_.size(); ++i) {
      const std::optional<RayHit3> hit = aether::Raycast(ray, boxes_[i]);
      if (hit && (!best || hit->t < best->t)) {
        best = GeometryHit3{.id = 2 + static_cast<U64>(i),
                            .t = hit->t,
                            .point = hit->point,
                            .normal = hit->normal};
      }
    }
    if (const std::optional<GeometryHit3> ramp = RampCast(ray);
        ramp && (!best || ramp->t < best->t)) {
      best = ramp;
    }
    return best;
  }

  [[nodiscard]] bool OverlapsSphere(const Sphere& s, U32) const override {
    for (const AABB& box : boxes_) {
      if (Overlaps(s, box)) {
        return true;
      }
    }
    return RampDistance(s.center) < s.radius;
  }

  // GEA §13.3.7.2, swept for real. THE LEVEL IS WHERE THE CONVEX-EDGE DEFECT
  // LIVES — a ray passing beside the block's corner sees nothing while the
  // capsule clips it — so a level query that only raycasts leaves the character
  // controller's improvement on the table, whatever the controller does.
  //
  // The ramp is a PLANE and the seam's ray approximation is exact for a plane,
  // so it keeps the same correction it always had; only the boxes needed a
  // real sweep.
  [[nodiscard]] ShapeCastHit3 SphereCast(
      const Sphere& sphere, Vec3 delta, U32 mask,
      std::span<ShapeContact3> out) const override {
    ShapeCastHit3 result;
    const F32 distance = Length(delta);
    constexpr F32 kMinDistance = 0.0001f;
    const Sphere probe{.center = sphere.center,
                       .radius = sphere.radius * 0.999f};
    if (OverlapsSphere(probe, mask)) {
      if (!out.empty()) {
        out[0] = ShapeContact3{.id = 0, .point = sphere.center, .normal = {}};
      }
      return ShapeCastHit3{
          .t = 0.0f, .started_penetrating = true, .contacts = 1};
    }
    if (distance < kMinDistance) {
      return result;
    }
    const Ray3 ray{.origin = sphere.center, .direction = delta / distance};
    const auto consider = [&](U64 id, const std::optional<RayHit3>& hit,
                              F32 travelled) {
      if (!hit || travelled < 0.0f || travelled > distance) {
        return;
      }
      const F32 t = travelled / distance;
      constexpr F32 kTie = 1e-4f;
      const ShapeContact3 contact{
          .id = id, .point = hit->point, .normal = hit->normal};
      if (t < result.t - kTie) {
        result.t = t;
        result.contacts = 1;
        if (!out.empty()) {
          out[0] = contact;
        }
      } else if (t <= result.t + kTie) {
        result.t = std::min(result.t, t);
        if (result.contacts < out.size()) {
          out[result.contacts] = contact;
        }
        ++result.contacts;
      }
    };
    for (Usize i = 0; i < boxes_.size(); ++i) {
      const std::optional<RayHit3> hit =
          SweepSphere(ray, sphere.radius, boxes_[i]);
      consider(2 + static_cast<U64>(i), hit, hit ? hit->t : -1.0f);
    }
    if (const std::optional<GeometryHit3> ramp = RampCast(ray); ramp) {
      const F32 cos_theta = std::abs(Dot(ray.direction, ramp->normal));
      if (cos_theta > 0.02f) {
        const RayHit3 as_hit{
            .t = ramp->t, .point = ramp->point, .normal = ramp->normal};
        consider(ramp->id, as_hit, ramp->t - sphere.radius / cos_theta);
      }
    }
    return result;
  }

 private:
  // The ramp's plane: y = kBlockTop at x = kKerbX1, falling to 0 over
  // `kRampRun`. Solid BELOW it, and bounded to the block's z span and the run.
  static Vec3 RampNormal() {
    const Vec3 n{kBlockTop, kRampRun, 0.0f};
    return Normalize(n);
  }
  static bool OnRamp(Vec3 at) {
    return at.x >= kKerbX1 && at.x <= kKerbX1 + kRampRun && at.z >= kBlockZ0 &&
           at.z <= kBlockZ1;
  }
  // Signed distance to the ramp plane; negative is inside the solid.
  static F32 RampDistance(Vec3 p) {
    if (!OnRamp(p)) {
      return 1e9f;
    }
    return Dot(p - Vec3{kKerbX1, kBlockTop, 0.0f}, RampNormal());
  }
  static std::optional<GeometryHit3> RampCast(const Ray3& ray) {
    const Vec3 n = RampNormal();
    const F32 denom = Dot(ray.direction, n);
    if (denom > -1e-4f) {
      return std::nullopt;  // parallel, or leaving rather than entering
    }
    const F32 t = Dot(Vec3{kKerbX1, kBlockTop, 0.0f} - ray.origin, n) / denom;
    if (t < 0.0f || !OnRamp(ray.At(t))) {
      return std::nullopt;
    }
    return GeometryHit3{.id = 9, .t = t, .point = ray.At(t), .normal = n};
  }

  std::array<AABB, 4> boxes_;
};


}  // namespace infiltration::content
