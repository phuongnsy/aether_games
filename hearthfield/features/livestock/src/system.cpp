#include "hf/features/livestock/system.hpp"

#include <algorithm>

#include "hf/content/animals.hpp"
#include "hf/runtime/chain.hpp"
#include "hf/runtime/tick.hpp"
#include "hf/runtime/world_view.hpp"

namespace hearthfield::livestock {
namespace {

using runtime::Coop;
using runtime::Tick;

[[nodiscard]] Tick FedSpan() {
  return runtime::TicksFromSeconds(content::kFedSeconds);
}
[[nodiscard]] Tick LayInterval() {
  return runtime::TicksFromSeconds(content::kLaySeconds);
}

}  // namespace

bool LivestockSystem::TryFeed(runtime::StepContext& ctx,
                              runtime::EventList& out) {
  Coop& coop = ctx.world.TheCoop();
  if (coop.animals == 0) {
    out.emplace_back(
        runtime::Refused{.why = runtime::Refused::Why::kNoSpaceInQueue});
    return false;
  }
  // ALL-OR-NOTHING AND FIRST, exactly as the mill takes its ingredients. This
  // line is the commit: after it, nothing the coop does depends on the barn.
  if (!ctx.world.TheBarn().Take(content::kFeedItem, content::kFeedPerFill)) {
    out.emplace_back(runtime::Refused{.why = runtime::Refused::Why::kNoItems});
    return false;
  }

  const Tick now = ctx.world.Now();
  // EXTENDS from whichever is later. Topping up a trough that still has feed in
  // it adds to what is there; filling a dry one starts from now, rather than
  // from a deadline that expired last week.
  const Tick from = std::max(now, coop.fed_until);
  coop.fed_until = from + FedSpan();

  // THE RESET, and it is not a detail. A coop that starved for a week has a
  // `next_lay` a week in the past; leaving it there would pay out the whole
  // drought the instant it was fed. The commit moment is the reset moment
  // (H5 plan §3b) — but only when the birds had actually stopped, or a top-up
  // would silently cancel the egg already on its way.
  if (coop.next_lay <= now) {
    coop.next_lay = now + LayInterval();
  }
  out.emplace_back(
      runtime::Fed{.spent = content::kFeedPerFill, .until = coop.fed_until});
  return true;
}

void LivestockSystem::Step(runtime::StepContext& ctx,
                           const runtime::EventList& /*in*/,
                           runtime::EventList& out) {
  if (ctx.input.feed_coop) {
    (void)TryFeed(ctx, out);
  }

  Coop& coop = ctx.world.TheCoop();
  if (coop.animals == 0) {
    return;
  }
  const Tick now = ctx.world.Now();
  const Tick interval = LayInterval();

  // The window that actually counts: feed ran out, or now, whichever came
  // first. One `min` is the entire offline model for this feature — a coop that
  // went dry six hours into an eight-hour absence lays for six.
  const Tick end = std::min(now, coop.fed_until);
  if (coop.next_lay <= end) {
    const Tick due = ((end - coop.next_lay) / interval) + 1;
    const auto eggs = static_cast<aether::U32>(due * content::kEggsPerLay);
    const aether::U32 stored =
        ctx.world.TheBarn().Add(content::kLaysItem, eggs);
    // Overflow is LOST, not banked, and the timer moves regardless — the same
    // bargain a harvest into a full barn already makes. Stalling instead would
    // make the result depend on when the barn was emptied, which is the
    // cross-entity coupling this whole design exists to avoid.
    coop.next_lay += due * interval;
    out.emplace_back(runtime::Laid{.laid = eggs});
    out.emplace_back(runtime::Deposited{
        .item = content::kLaysItem, .stored = stored, .lost = eggs - stored});
  }

  // Said ONCE, on the step that crosses the deadline — including the step that
  // absorbed the whole absence, since `fed_until` is inside the jump.
  // `fed_until > 0` excludes a coop that was NEVER fed: it is hungry, but it
  // has not just BECOME hungry, and firing on step 1 of a new farm would make
  // this an announcement rather than an event.
  if (coop.fed_until > 0 && now > coop.fed_until &&
      now - (Tick{1} + ctx.input.offline_ticks) <= coop.fed_until) {
    out.emplace_back(runtime::CoopHungry{});
  }
}

}  // namespace hearthfield::livestock
