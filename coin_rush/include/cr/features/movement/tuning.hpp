// Movement feel constants (weighty/tight). The jump is DESIGNED from apex
// height H + rise time T (gravity 2H/T^2, launch 2H/T); re-derive if retuned.
#pragma once

#include "aether/core/types.hpp"

namespace game {

constexpr aether::F32 kRunSpeed = 220.0f;      // top run speed (~4.6 tiles/s)
constexpr aether::F32 kGroundAccel = 3200.0f;  // ramp to top speed in ~0.07 s
constexpr aether::F32 kGroundFriction = 3600.0f;  // stop in ~0.06 s (not icy)
constexpr aether::F32 kGroundTurn = 5600.0f;    // skid: flip direction crisply
constexpr aether::F32 kAirAccel = 2600.0f;      // strong, precise air control
constexpr aether::F32 kAirFriction = 900.0f;    // a little drift → air momentum
constexpr aether::F32 kAirTurn = 4000.0f;       // reverse mid-air, still snappy
constexpr aether::F32 kGravity = 2450.0f;       // base RISE gravity (weighty)
constexpr aether::F32 kJumpVel = 735.0f;        // launch → a ~2.3-tile apex
constexpr aether::F32 kFallGravityMul = 1.7f;   // fall faster than you rose
constexpr aether::F32 kLowJumpMul = 2.6f;       // released while rising → hop
constexpr aether::F32 kApexVel = 85.0f;         // |vy| under this = apex hang
constexpr aether::F32 kApexGravityMul = 0.78f;  // subtle float for apex control
constexpr aether::F32 kMaxFall = 1150.0f;       // terminal velocity (weight)
constexpr aether::F32 kCoyote = 0.10f;       // grace to jump just off a ledge
constexpr aether::F32 kJumpBufferT = 0.12f;  // jump before landing still fires
constexpr aether::F32 kGroundSnap = 5.0f;    // ground-detect tolerance
constexpr aether::F32 kWindOnGround = 1.2f;  // wind→accel gain while grounded
constexpr aether::F32 kWindAirborne = 3.0f;  // …stronger mid-air
constexpr aether::F32 kWetSlip = 0.55f;      // wet traction cut (not icy)

}  // namespace game
