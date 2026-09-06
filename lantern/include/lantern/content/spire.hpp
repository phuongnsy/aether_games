// The spire's declarative facts: where the world lives, and the numbers that
// decide how it plays. Content, not code — `runtime` and the features read
// these and nothing here reads them back.
#pragma once

#include "aether/core/types.hpp"

namespace lantern::content {

using aether::F32;
using aether::Usize;

inline constexpr const char* kWorldPath = "worlds/spire.world.json";
inline constexpr const char* kCharacterPath = "models/walker.gltf";

// Reach is a COLUMN, not a sphere: a lantern hangs above its platform, so you
// light it by standing UNDER it, and how high it hangs is the level author's
// business rather than the player's problem. A spherical reach made lantern 0
// unlightable — it hangs 3 m over a platform, so a walker standing directly
// beneath it was 2.6 m away and out of range while visibly under the thing.
inline constexpr F32 kLightRadius = 2.2f;  // horizontal
// How far BELOW a lantern still counts. Bounded so you cannot light a lantern
// from the tier under its platform, which would skip the climb it marks.
inline constexpr F32 kLightReach = 3.8f;

// Below this you have fallen off the spire and return to your last lantern.
// Set under the ground plane rather than at it: landing ON the ground is a
// legitimate place to be, and only leaving the world is a fall.
inline constexpr F32 kFallY = -3.0f;

// Where you start, and where an unlit run returns you to.
inline constexpr F32 kStartX = 0.0f;
inline constexpr F32 kStartY = 1.0f;
inline constexpr F32 kStartZ = 5.0f;

}  // namespace lantern::content
