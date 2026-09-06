// Livestock: a coop that eats what it was given and lays on a timer.
//
// THE FEATURE THE SPEC NAMED AS THE TEST OF RISK 5, and it does not get an
// exemption. ADR-0108's rule — commit at commit time, never at consume time —
// is honoured by the trough: filling it debits the barn and converts wheat into
// `fed_until`, an absolute tick. A hen therefore never reaches into the barn,
// its future depends on two numbers it owns and the clock, and an eight-hour
// absence stays arithmetic.
//
// THE CATCH-UP IS INTEGER DIVISION, NOT GEA §16.8.9.1's DISPATCH LOOP, and the
// difference from `production` next door is deliberate. That loop's bound is
// the queue depth, four. This one's bound is how long you were away — 30 days
// at 60 Hz is 155 million intervals, so a loop is the wrong shape however
// correct it is. The order board's catch-up has the same bound and the same
// answer (H5 plan §3d).
#pragma once

#include "hf/runtime/events.hpp"
#include "hf/runtime/sim_system.hpp"

namespace hearthfield::livestock {

class LivestockSystem final : public runtime::SimSystem {
 public:
  void Step(runtime::StepContext& ctx, const runtime::EventList& in,
            runtime::EventList& out) override;

 private:
  // Debit the barn and extend the trough. Refuses (and says so) when the feed
  // is not there or there are no birds to eat it.
  static bool TryFeed(runtime::StepContext& ctx, runtime::EventList& out);
};

}  // namespace hearthfield::livestock
