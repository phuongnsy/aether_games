// The guards: what they know, and the policy that decides what they do.
//
// A FEATURE layer, and a sim one — the build refuses it aether::scene and
// aether::render. The state was seven parallel arrays on a game class; the
// policy was a Lua string beside them. Neither moved by behaviour (i2), and
// the four capture digests are what says so.
#pragma once

#include <array>
#include <string>

#include "aether/ai/perception.hpp"
#include "aether/anim/locomotion.hpp"
#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "aether/core/geometry_query.hpp"
#include "aether/nav/crowd.hpp"

namespace infiltration::features::guards {

using namespace aether;  // NOLINT(google-build-using-namespace)

// Two, and the number is the scope: enough for a patrol and a responder.
constexpr Usize kGuards = 2;

// --- the guards' policy ------------------------------------------------------
// GEA §16.9.5.6: a finite state machine ON THE SCRIPT SIDE. The engine owns no
// tree and no scorer, and the state-to-posture mapping is here rather than in
// C++ because a posture table IS policy (ADR-0190/0192).
constexpr const char* kPolicySource = R"LUA(
local kPatrol = { { x = -7.0, z = -6.0 }, { x = -7.0, z = 6.0 } }

-- Authored cover points. Three are behind the wall from the +z half; the
-- fourth is in the open on purpose, so cover_choose has something to reject.
local kCover = {
  { x = -8.0, z = -4.0 },
  { x = -2.0, z = -3.0 },
  { x =  2.0, z = -5.0 },
  { x =  0.0, z =  3.0 },
}

-- TWO THRESHOLDS, NOT ONE. With a single 0.30 the guards flip-flopped
-- alert<->investigate every few steps while confidence hovered at 0.29..0.33 —
-- visible in the transition log as a dozen changes a second, which is the
-- state-level version of the popping GEA 14.4.5.3 warns about for gains.
local kSpotted = 0.30  -- enter alert
local kLost = 0.15     -- and do not leave until well below it

function agent_policy(i)
  local state = bb_get(i, "state") or "patrol"
  local conf = bb_get(i, "confidence") or 0.0
  local remembered = bb_get(i, "remembered")
  local arrived = bb_get(i, "arrived")
  local leg = bb_get(i, "leg") or 1

  if state == "alert" then
    -- Hysteresis: stay alert until confidence falls well below the entry
    -- threshold, then go and look where the player was last.
    if conf < kLost then
      if remembered then state = "investigate" else state = "patrol" end
    end
  elseif conf > kSpotted then
    state = "alert" 
  elseif state == "investigate" then
    if arrived or conf < 0.05 then state = "patrol" end
  elseif state == "patrol" then
    if arrived then
      leg = 3 - leg
      bb_set(i, "leg", leg)
    end
  end

  local wx, wz = nil, nil
  if state == "alert" then
    -- Break the line of sight: the player is the threat, and the authored
    -- points are the candidates. No table crosses the seam, so they are pushed
    -- one at a time.
    cover_reset()
    cover_threat(bb_get(i, "target_x"), 1.2, bb_get(i, "target_z"))
    for k = 1, #kCover do
      cover_candidate(kCover[k].x, 1.2, kCover[k].z)
    end
    -- `local` MATTERS: Lua's conventional `_` throwaway is a GLOBAL without
    -- it, and SealGlobals refuses a new global at run time. The sandbox caught
    -- this on the first run, which is the seal doing its job.
    local cx, _cy, cz = cover_choose(bb_get(i, "at_x"), 1.2, bb_get(i, "at_z"))
    wx, wz = cx, cz
  elseif state == "investigate" then
    wx, wz = bb_get(i, "last_x"), bb_get(i, "last_z")
  end
  if wx == nil then
    wx, wz = kPatrol[leg].x, kPatrol[leg].z
  end

  local posture = 0.0
  if state == "alert" then posture = 1.0
  elseif state == "investigate" then posture = 0.45 end

  bb_set(i, "state", state)
  bb_set(i, "posture", posture)
  bb_set(i, "want_x", wx)
  bb_set(i, "want_z", wz)
end
)LUA";

// One guard's worth per index, together rather than in seven arrays that a
// reader has to keep aligned by hand.
struct GuardState {
  std::array<nav::AgentId, kGuards> id{};
  std::array<Vec3, kGuards> target{};
  std::array<anim::LocomotionState, kGuards> loco{};
  std::array<F32, kGuards> phase{};
  std::array<ai::Awareness, kGuards> aware{};
  std::array<std::string, kGuards> reported{};
  std::array<bool, kGuards> disabled{};
};


// One guard's gait for one step: the crowd moved it, this says what that
// looks like. ADR-0184's solver does not care that the velocity came from a
// crowd rather than a keyboard — the player's step calls the same one.
inline void StepGuardGait(GuardState& guard, Usize g, Vec3 velocity,
                          const anim::LocomotionConfig& loco, F32 dt) {
  guard.loco[g] = anim::SolveLocomotion(velocity, guard.loco[g].facing, loco, dt);
  guard.phase[g] += dt * guard.loco[g].rate;
}

// What one guard can see this step, and what that does to what it believes.
// The raw sighting is not returned: nothing outside wanted it, which the
// compiler said the moment the two lines became one call.
inline void StepGuardAwareness(GuardState& guard, Usize g, Vec3 eye,
                               Vec3 forward, Vec3 chest,
                               const GeometryQuery3& walls,
                               const ai::VisionCone& cone, F32 dt) {
  const bool seen = ai::CanSee(eye, forward, chest, walls, cone);
  guard.aware[g] = ai::UpdateAwareness(guard.aware[g], seen, chest, dt);
}
}  // namespace infiltration::features::guards
