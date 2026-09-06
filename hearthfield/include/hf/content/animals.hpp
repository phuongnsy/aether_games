// The coop's tuning — declarative data, like the crop and recipe tables.
//
// Durations are SECONDS for the same reason those are: the fixed step is a
// config key, and a table written in ticks would rescale the whole economy when
// it moved, reaching files already on disk (ADR-0106).
#pragma once

#include <string_view>

#include "aether/core/types.hpp"
#include "hf/content/items.hpp"

namespace hearthfield::content {

// Four birds, and the number is fixed on purpose: buying more is economy work
// and this milestone is scoped against check 8 (H5 plan §3h).
inline constexpr aether::U8 kFlockSize = 4;

// What one fill of the trough costs and how long it lasts. The pair IS the
// design: a fill is affordable off a single wheat harvest (a crop yields 2) and
// runs out inside a working day, so a player who checks in each evening keeps
// the birds laying and one who forgets for a week loses the tail of it.
inline constexpr ItemId kFeedItem = kWheatItem;
inline constexpr aether::U32 kFeedPerFill = 4;
inline constexpr aether::F64 kFedSeconds = 3600.0;  // one hour of eating

// Slower than wheat and faster than the mill, so the coop is the thing that
// pays out while you are away and the mill is the thing worth coming back for.
inline constexpr aether::F64 kLaySeconds = 900.0;
inline constexpr ItemId kLaysItem = kEgg;
inline constexpr aether::U32 kEggsPerLay = 1;

// One mesh for the whole flock, animated per bird. The model is skinned because
// the point of the flock is Phase E's column getting a third consumer.
inline constexpr std::string_view kChickenModel = "models/chicken.gltf";
// A hen is really 0.3 m, which under this camera is eighteen pixels of bird.
// The MODEL stays anatomically sized so it can be reused; the farm stylises it
// up, the way the genre does. A readability decision, not a modelling error.
inline constexpr aether::F32 kChickenScale = 2.2f;
inline constexpr std::string_view kPeckClip = "peck";
inline constexpr std::string_view kIdleClip = "idle";

}  // namespace hearthfield::content
