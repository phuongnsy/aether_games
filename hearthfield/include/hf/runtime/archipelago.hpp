// Which islands the player owns, and which one they are standing on.
//
// Its own header rather than a member of `chain.hpp`, because the chain is the
// barn, the buildings, the orders and the purse — a farm's economy — and this
// is a fact about the WORLD the farm sits in. The two meet at exactly one
// point: an island is bought with coin.
#pragma once

#include "aether/core/types.hpp"
#include "hf/content/islands.hpp"
#include "hf/runtime/chain.hpp"  // Economy — an island is bought with coin

namespace hearthfield::runtime {

// THE UNLOCKED SET IS A FIXED-WIDTH BITMASK, and that is a save-format decision
// before it is a runtime one. A list would need a length prefix, and a bare run
// would make `content::kIslands.size()` part of the save's geometry — the exact
// defect `kSaveFormat`'s kItemCountV2 comment records, where v2 wrote the barn
// as a lengthless run of item counts and appending an item would have corrupted
// every file on disk. One U32 has the same geometry whatever the table's
// length; islands.hpp static_asserts the 32 that buys.
struct Archipelago {
  // Bit i is island i. THE HUB IS ALWAYS BIT 0 AND ALWAYS SET — a farm you
  // cannot reach is not a recoverable state, so it is an invariant rather than
  // a starting value, restored on load (see IslandsFromSave).
  aether::U32 unlocked = 1u << content::kHub;
  // Where the player is. Persisted so a relaunch puts them back where they
  // stood; s5 is what lets it change.
  content::IslandId current = content::kHub;
};

[[nodiscard]] constexpr bool IsUnlocked(const Archipelago& isles,
                                        content::IslandId id) {
  return content::IslandExists(id) && (isles.unlocked & (1u << id)) != 0;
}

// Why an unlock was refused. An enum rather than a bool because the three
// answers want three different things said to the player, and a `false` that
// means "already yours" reads as a failure when it is not.
enum class UnlockResult : aether::U8 {
  kBought,
  kAlreadyOwned,
  kNoSuchIsland,
  kCannotAfford,
};

// Buy one island. THE ONLY WAY `unlocked` GAINS A BIT outside a load, so the
// price and the purse cannot disagree: both are read here or nowhere.
//
// No UI reaches this yet (s4 defers the shop entry to s5, when there is
// somewhere to go and the purchase has a visible consequence). It is a verb
// with a test and no button, deliberately — the state it writes is what the
// save migration exists to carry.
[[nodiscard]] constexpr UnlockResult UnlockIsland(Archipelago& isles,
                                                  Purse& purse,
                                                  content::IslandId id) {
  const content::Island* island = content::FindIsland(id);
  if (island == nullptr) {
    return UnlockResult::kNoSuchIsland;
  }
  if (IsUnlocked(isles, id)) {
    return UnlockResult::kAlreadyOwned;
  }
  if (purse.coin < island->unlock_coins) {
    return UnlockResult::kCannotAfford;
  }
  purse.coin -= island->unlock_coins;
  isles.unlocked |= (1u << id);
  return UnlockResult::kBought;
}

// What a loaded file's two fields mean once this build has looked at them.
//
// A SAVE IS NOT TRUSTED HERE, and the reasons are different for each field. The
// mask may name islands this build does not have — a save written by a later
// version, or one whose table shrank — and those bits are dropped rather than
// left to index off the end of kIslands. `current` may name an island that is
// not unlocked or does not exist, which would strand the player somewhere they
// cannot be; the hub is the answer to both, because it is the one island that
// is always theirs.
[[nodiscard]] constexpr Archipelago IslandsFromSave(aether::U32 unlocked,
                                                    aether::U32 current) {
  const aether::U32 present = content::kIslands.size() >= 32
                                  ? ~0u
                                  : ((1u << content::kIslands.size()) - 1u);
  Archipelago isles;
  isles.unlocked = (unlocked & present) | (1u << content::kHub);
  const auto id = static_cast<content::IslandId>(current);
  isles.current = IsUnlocked(isles, id) ? id : content::kHub;
  return isles;
}

}  // namespace hearthfield::runtime
