// Economy: the purse, the barn's contents, and how much of the farm is yours.
//
// Runs LAST (spec §5.1), so it sees every event the earlier systems emitted
// this step — a crop harvested this step reaches the barn this step. It reads
// everything and owns the progression curve.
#pragma once

#include "hf/runtime/events.hpp"
#include "hf/runtime/sim_system.hpp"

namespace hearthfield::economy {

// A new farm starts with a corner of the board and enough coin to matter.
inline constexpr aether::U32 kStartingPlots = 4;
inline constexpr aether::U32 kStartingCoin = 20;

// Land gets dearer as the farm grows, which is what stops coin from being a
// number that only goes up.
inline constexpr aether::U32 kLandBasePrice = 25;
inline constexpr aether::U32 kLandPriceStep = 15;

[[nodiscard]] constexpr aether::U32 LandPrice(aether::U32 unlocked) {
  const aether::U32 bought =
      unlocked > kStartingPlots ? unlocked - kStartingPlots : 0;
  return kLandBasePrice + (kLandPriceStep * bought);
}

// Barn capacity, the second coin sink. Already saved and digested (v2 carries
// Barn::capacity), so it costs a verb and no format change.
inline constexpr aether::U32 kStartingCapacity = 50;
inline constexpr aether::U32 kBarnStep = 25;  // units bought per upgrade
inline constexpr aether::U32 kBarnBasePrice = 40;
inline constexpr aether::U32 kBarnPriceStep = 30;

[[nodiscard]] constexpr aether::U32 BarnPrice(aether::U32 capacity) {
  const aether::U32 bought = capacity > kStartingCapacity
                                 ? (capacity - kStartingCapacity) / kBarnStep
                                 : 0;
  return kBarnBasePrice + (kBarnPriceStep * bought);
}

// A seed packet: raw goods for coin, so a player who would rather run the mill
// than the field has something to spend on. Priced ABOVE what an order pays for
// the same item, or buying low and selling high would be the whole game.
inline constexpr aether::U32 kPacketSize = 5;

[[nodiscard]] constexpr aether::U32 PacketPrice(aether::U32 count) {
  return count * 5;
}

class EconomySystem final : public runtime::SimSystem {
 public:
  void Step(runtime::StepContext& ctx, const runtime::EventList& in,
            runtime::EventList& out) override;
};

}  // namespace hearthfield::economy
