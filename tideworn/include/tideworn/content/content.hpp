// Tideworn's declarative data: asset paths and the voyage's sea-state
// schedule. Data, not code (AGENTS.md) — systems read these, they never
// parse files.
#pragma once

#include <array>

#include "aether/core/types.hpp"

namespace tideworn::content {

inline constexpr const char* kEnvPath = "env/ocean.env.json";
inline constexpr const char* kBoatPath = "models/boat_hull.gltf";
inline constexpr const char* kFishPath = "models/fish.gltf";
inline constexpr const char* kDiverPath = "models/diver.gltf";

// The time-of-day keyframe ring: the SAME physical ocean sky baked at eight
// sun positions (env_studio's parametric sun; the bake commands live in
// tools/asset_rules.json). `at` is the day fraction the keyframe represents;
// runtime crossfades the two neighbours and drives the directional light from
// their manifests' derived sun — one authored input, zero hand-kept tables.
struct SkyKey {
  const char* env;
  aether::F32 at;
};
inline constexpr std::array<SkyKey, 8> kSkyRing = {
    SkyKey{.env = "env/ocean_t000.env.json", .at = 0.000f},
    SkyKey{.env = "env/ocean_t125.env.json", .at = 0.125f},
    SkyKey{.env = "env/ocean_t250.env.json", .at = 0.250f},
    SkyKey{.env = "env/ocean_t375.env.json", .at = 0.375f},
    SkyKey{.env = "env/ocean_t500.env.json", .at = 0.500f},
    SkyKey{.env = "env/ocean_t625.env.json", .at = 0.625f},
    SkyKey{.env = "env/ocean_t750.env.json", .at = 0.750f},
    SkyKey{.env = "env/ocean_t875.env.json", .at = 0.875f}};

// One keyframe of the voyage's sea state: at `at_s` on the voyage clock the
// wind is `wind_mps`. Linearly interpolated, looping over the last key's time.
struct WindKey {
  aether::F32 at_s;
  aether::F32 wind_mps;
};

// One calm -> gale -> storm -> calm cycle per in-game day. The RANGE is the
// point (ADR-0057): wind is the one input the whole sea derives from, so the
// voyage's drama is authored here and nowhere else.
inline constexpr std::array<WindKey, 7> kWindSchedule = {
    WindKey{.at_s = 0.0f, .wind_mps = 5.0f},
    WindKey{.at_s = 120.0f, .wind_mps = 7.0f},
    WindKey{.at_s = 240.0f, .wind_mps = 12.0f},
    WindKey{.at_s = 330.0f, .wind_mps = 18.0f},
    WindKey{.at_s = 420.0f, .wind_mps = 24.0f},
    WindKey{.at_s = 510.0f, .wind_mps = 12.0f},
    WindKey{.at_s = 600.0f, .wind_mps = 5.0f}};

// Seconds of voyage clock per full day/night cycle. Matches the wind
// schedule's period so p0's one scene shows every state; p2 decouples them.
inline constexpr aether::F32 kDayLengthS = 600.0f;

// One marine-life species: a depth band it lives in, a school anchored near
// the boat (the diving loop happens where the player is). One fish model for
// all — `scale` is the species; count x scale is the whole art budget.
struct Species {
  aether::U32 count;
  aether::F32 scale;      // metres nose to tail (the model is 1 m)
  aether::F32 depth_min;  // band below the surface, metres (positive down)
  aether::F32 depth_max;
  aether::F32 cruise_mps;   // comfortable speed; flee doubles it
  aether::F32 home_radius;  // xz containment around the anchor
};
inline constexpr std::array<Species, 3> kSpecies = {
    Species{.count = 18,
            .scale = 0.3f,
            .depth_min = 1.5f,
            .depth_max = 4.0f,
            .cruise_mps = 0.9f,
            .home_radius = 6.0f},
    Species{.count = 7,
            .scale = 0.6f,
            .depth_min = 5.0f,
            .depth_max = 10.0f,
            .cruise_mps = 0.6f,
            .home_radius = 9.0f},
    Species{.count = 1,
            .scale = 1.8f,
            .depth_min = 12.0f,
            .depth_max = 20.0f,
            .cruise_mps = 0.4f,
            .home_radius = 14.0f}};

}  // namespace tideworn::content
