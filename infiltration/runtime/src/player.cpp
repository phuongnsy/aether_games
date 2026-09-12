// The runtime layer's translation unit — see content/src/level.cpp for why a
// header-only layer still gets one.
#include <algorithm>
#include <cmath>

#include "infiltration/content/level.hpp"
#include "infiltration/runtime/player.hpp"

namespace infiltration::runtime {

void StepPlayer(PlayerState& player, Vec3 intent, const GeometryQuery3& walls,
                std::span<const nav::CharacterCapsule> capsules, U64 self_id,
                F32 half_extent, const anim::LocomotionConfig& loco, F32 dt) {
    // ACCELERATION STAYS HERE, COLLISION GOES THERE. `nav::MoveCharacter` owns
    // the sweep, the slide, the ground and the slope cutoff; gaits, intent and
    // the accel/brake feel are a game's business and stay at the call site.
    const F32 rate = LengthSquared(intent) > 1e-6f ? player.tune.accel : player.tune.brake;
    const Vec3 gap = intent - player.desired;
    const F32 step = rate * dt;
    player.desired = Length(gap) <= step ? intent : player.desired + Normalize(gap) * step;

    // b1 — THE GUARDS ARE PART OF THE WORLD NOW (GEA 13.5.3.6, ADR pending).
    // Until 2026-09-10 the player walked straight through them, which is what
    // let ADR-0202's reach land a takedown while AIMED STRAIGHT UP: the
    // autopilot closed to 0.4 m, the player stood inside the guard's hit boxes,
    // and a vertical ray found a thigh 0.11 m up.
    //
    // The blocker is the guard's MOVEMENT capsule, not its sixteen hit boxes —
    // 13.5.3.6 keeps those for "bullet hit detection" and moves the character
    // with a capsule cast, which is what `MoveCharacter` is.
    const nav::CharacterObstacles world{walls, capsules, self_id};
    player.motion = nav::MoveCharacter(player.motion, player.desired, world, player.body, dt);
    // ...and then RESOLVED, because seeing is not the same as being kept out.
    // A ray offset laterally by more than a capsule's radius misses it
    // entirely, so the probe above never sees a guard the player walks PAST —
    // measured at 0.41 m of a stationary guard's centre against the 0.70 m the
    // two capsules occupy. §13.5.3.6: "Collisions are resolved manually."
    player.motion.position =
        nav::SeparateFromCharacters(player.motion.position, player.body.radius,
                                    player.body.height, capsules, self_id);
    // The floor is 20 m square with no parapet, so the edge is a clamp rather
    // than geometry — the level's omission, not the controller's.
    player.motion.position.x = std::clamp(player.motion.position.x, -half_extent + player.body.radius,
                                    half_extent - player.body.radius);
    player.motion.position.z = std::clamp(player.motion.position.z, -half_extent + player.body.radius,
                                    half_extent - player.body.radius);
    player.position = player.motion.position;
    // §13.5.3.6's fifth bullet, and it is one assignment: the ACHIEVED velocity
    // is what the locomotion solver sees, so a player pressed into the wall
    // stops walking on the spot instead of moonwalking.
    player.velocity = player.motion.velocity;
    // ADR-0184's SOLVER, ON A PLAYER. It takes a velocity and returns gait
    // indices, a blend, a rate and a yaw — and it does not care that this
    // velocity came from a keyboard rather than from a crowd. That is the claim
    // the interface makes, tested here for the first time.
    player.loco = anim::SolveLocomotion(player.velocity, player.loco.facing,
                                         loco, dt);
    player.phase += dt * player.loco.rate;
}

}  // namespace infiltration::runtime
