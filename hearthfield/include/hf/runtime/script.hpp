// The scripted session: a canned input stream, as DATA.
//
// `games/CLAUDE.md` names the destination — "the headless capture autopilot IS
// a canned input script (an InputSource)" — and until H6 this game's was not
// one. It lived inside `Update`, keyed off the render frame counter and mixed
// with pointer handling, so the only thing that could play it was the
// executable with a window open.
//
// That mattered for one reason: **a digest stamped from a run nobody can
// reproduce is not an oracle.** Check 9 asks for a stamped digest, and the
// session it hashes has to be replayable by a headless test. Here it is one
// function of one integer, so app/ and the test drive the same beats.
//
// It plays the WHOLE chain — sow, wait, reap, feed, refine, ship, expand — and
// the waiting is done with `offline_ticks` rather than by running the frames,
// because an hour of grinding is not something a capture should sit through.
#pragma once

#include "aether/core/types.hpp"
#include "hf/content/animals.hpp"
#include "hf/content/crops.hpp"
#include "hf/content/recipes.hpp"
#include "hf/runtime/latched_input.hpp"
#include "hf/runtime/tick.hpp"

namespace hearthfield::runtime {

// Everything a beat needs to know about the farm it is playing on. Passed in
// rather than read, so the script depends on no world and stays a pure
// function — which is what lets a test call it with nothing built.
struct ScriptContext {
  aether::U32 plot_count = 1;
  // The first order slot the barn can actually satisfy, or kNoSlot. Resolved by
  // the CALLER because it is a question about live state: a script that named a
  // fixed slot would be shipping whatever the RNG happened to post there, which
  // demonstrates nothing.
  aether::U8 fillable_slot = LatchedInput::kNoSlot;
  // How long one order-board refresh takes, in ticks. SUPPLIED, not spelled:
  // it is `orders::kRefreshSeconds` and `runtime` may not depend on a feature.
  // Writing the number here instead was tried and silently shortened the
  // session six-fold — the script still ran, still ended, and produced a
  // different farm. A tuning constant copied is a tuning constant that drifts.
  Tick order_refresh = 0;
};

// How many beats the script has. A run shorter than this plays a prefix.
inline constexpr aether::U32 kScriptBeats = 20;

// One beat's input. Beat 0 is the idle beat before anything happens.
//
// EVERY BEAT DOES SOMETHING, and there is a test that says so. A beat that
// silently produced no input would shorten the session without shortening the
// script, and the digest would still look stamped.
[[nodiscard]] inline LatchedInput ScriptBeat(aether::U32 beat,
                                             const ScriptContext& ctx) {
  LatchedInput in;
  const aether::U32 plots = ctx.plot_count == 0 ? 1 : ctx.plot_count;
  const auto tap = [&](aether::U32 which) {
    in.hovered = static_cast<PlotId>(which % plots);
    in.tap = true;
  };
  switch (beat) {
    case 1:
    case 2:
    case 3:
    case 4:  // sow the corner the player starts with
      tap(beat - 1);
      break;
    case 5:  // wait for the wheat
      in.offline_ticks = static_cast<aether::U32>(
          TicksFromSeconds(content::CropById(content::kWheat).grow_seconds));
      break;
    case 6:
    case 7:
    case 8:
    case 9:  // reap it into the barn
      tap(beat - 6);
      break;
    case 10:
      // FEED FIRST, and the ordering is the point: four plots of wheat yield
      // eight, a fill costs four and a grind two, so a script that ground first
      // would reach the trough with nothing left and quietly demonstrate a
      // refusal. Found by reading the numbers, not by the script failing.
      in.feed_coop = true;
      break;
    case 11:
    case 12:  // refine: two grinds, both committed now
      in.queue_recipe = content::kGrindWheat;
      break;
    case 13:  // wait for the mill, and for the first eggs
      in.offline_ticks = static_cast<aether::U32>(
          2 *
          TicksFromSeconds(content::RecipeById(content::kGrindWheat).seconds));
      break;
    case 14:
      // ...and for the board to fill. An order the farm cannot meet is the
      // design, not a bug (spec §1: "then it asks for something you have no
      // building for yet"), so this waits for three postings rather than hoping
      // the first one is fillable.
      in.offline_ticks = static_cast<aether::U32>(3 * ctx.order_refresh);
      break;
    case 15:
    case 16:
    case 17:  // ship whatever the barn can satisfy
      in.fill_slot = ctx.fillable_slot;
      break;
    case 18:
    case 19:  // expand, as far as the purse goes
      in.buy_land = true;
      break;
    default:
      break;
  }
  return in;
}

}  // namespace hearthfield::runtime
