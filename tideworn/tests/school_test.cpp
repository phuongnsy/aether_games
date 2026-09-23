// The school step's contract: deterministic, band-respecting, boat-fleeing.
// Headless by construction (AGENTS.md, dependency law 4).
#include <doctest/doctest.h>

#include <cmath>

#include "tideworn/content/content.hpp"
#include "tideworn/features/school/system.hpp"

using namespace aether;
using namespace tideworn;

namespace {

school::SchoolState Settled(U8 species, F32 seconds) {
  school::SchoolState state;
  school::Spawn(state, species, Vec2{0.0f, 0.0f}, 42);
  const school::Env env{.anchor_xz = Vec2{0.0f, 0.0f}, .threat_count = 0};
  const F32 dt = 1.0f / 60.0f;
  for (F32 t = 0.0f; t < seconds; t += dt) {
    school::Step(state, env, dt);
  }
  return state;
}

}  // namespace

TEST_CASE("school step is deterministic") {
  const school::SchoolState a = Settled(0, 10.0f);
  const school::SchoolState b = Settled(0, 10.0f);
  REQUIRE(a.fish.size() == b.fish.size());
  for (Usize i = 0; i < a.fish.size(); ++i) {
    CHECK(a.fish[i].pos.x == b.fish[i].pos.x);
    CHECK(a.fish[i].pos.y == b.fish[i].pos.y);
    CHECK(a.fish[i].pos.z == b.fish[i].pos.z);
  }
}

TEST_CASE("every species holds its depth band and home radius") {
  for (U8 s = 0; s < static_cast<U8>(content::kSpecies.size()); ++s) {
    const content::Species& kind = content::kSpecies[s];
    const school::SchoolState state = Settled(s, 60.0f);
    REQUIRE(state.fish.size() == kind.count);
    for (const school::Fish& f : state.fish) {
      // Soft springs allow a small excursion; a metre past the band or
      // half again the radius means containment failed, not feel.
      CHECK(f.pos.y < -kind.depth_min + 1.0f);
      CHECK(f.pos.y > -kind.depth_max - 1.0f);
      CHECK(std::hypot(f.pos.x, f.pos.z) < kind.home_radius * 1.5f);
      CHECK(std::isfinite(f.pos.x + f.pos.y + f.pos.z));
      const F32 speed = std::sqrt(Dot(f.vel, f.vel));
      CHECK(speed <= kind.cruise_mps * 2.0f + 1e-3f);
    }
  }
}

TEST_CASE("a school keeps up with a boat under way") {
  // The regression that hid every fish: the boat sails at 1.2 m/s, cruise is
  // 0.9, and without the catch-up ceiling the school fell astern forever.
  school::SchoolState state;
  school::Spawn(state, 0, Vec2{0.0f, 0.0f}, 42);
  const F32 dt = 1.0f / 60.0f;
  Vec2 boat{0.0f, 0.0f};
  for (F32 t = 0.0f; t < 120.0f; t += dt) {
    boat.x += 1.2f * dt;
    school::Step(
        state,
        school::Env{
            .anchor_xz = boat, .threats_xz = {{boat}}, .threat_count = 1},
        dt);
  }
  const content::Species& kind = content::kSpecies[0];
  for (const school::Fish& f : state.fish) {
    CHECK(std::hypot(f.pos.x - boat.x, f.pos.z - boat.y) <
          kind.home_radius + 6.0f);
  }
}

TEST_CASE(
    "the second threat slot works — fish flee a diver, not just the "
    "hull") {
  school::SchoolState state = Settled(0, 20.0f);
  Vec3 centroid{};
  for (const school::Fish& f : state.fish) {
    centroid = centroid + f.pos;
  }
  centroid = centroid * (1.0f / static_cast<F32>(state.fish.size()));
  // The hull far away, the DIVER on the school — exactly GameWorld's wiring.
  const school::Env env{
      .anchor_xz = Vec2{0.0f, 0.0f},
      .threats_xz = {{Vec2{100.0f, 100.0f}, Vec2{centroid.x, centroid.z}}},
      .threat_count = 2};
  F32 before = 0.0f;
  for (const school::Fish& f : state.fish) {
    before += std::hypot(f.pos.x - centroid.x, f.pos.z - centroid.z);
  }
  for (int i = 0; i < 180; ++i) {
    school::Step(state, env, 1.0f / 60.0f);
  }
  F32 after = 0.0f;
  for (const school::Fish& f : state.fish) {
    after += std::hypot(f.pos.x - centroid.x, f.pos.z - centroid.z);
  }
  CHECK(after > before);
}

TEST_CASE("fish flee a close threat") {
  school::SchoolState state = Settled(0, 20.0f);
  // Drop the threat on the school's centroid and watch the ring open.
  Vec3 centroid{};
  for (const school::Fish& f : state.fish) {
    centroid = centroid + f.pos;
  }
  centroid = centroid * (1.0f / static_cast<F32>(state.fish.size()));
  const school::Env threat{.anchor_xz = Vec2{0.0f, 0.0f},
                           .threats_xz = {{Vec2{centroid.x, centroid.z}}},
                           .threat_count = 1};
  F32 before = 0.0f;
  for (const school::Fish& f : state.fish) {
    before += std::hypot(f.pos.x - centroid.x, f.pos.z - centroid.z);
  }
  for (int i = 0; i < 180; ++i) {
    school::Step(state, threat, 1.0f / 60.0f);
  }
  F32 after = 0.0f;
  for (const school::Fish& f : state.fish) {
    after += std::hypot(f.pos.x - centroid.x, f.pos.z - centroid.z);
  }
  CHECK(after > before);
}
