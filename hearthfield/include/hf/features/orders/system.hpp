// The order board: what the village wants, and what it pays.
//
// The one feature that draws random numbers, which is why the RNG state is in
// the save (spec §4.4). Filling an order is economy's job — this only decides
// what appears.
#pragma once

#include "hf/runtime/events.hpp"
#include "hf/runtime/sim_system.hpp"
#include "hf/runtime/tick.hpp"

namespace hearthfield::orders {

// Half an hour between postings. Long enough that the board is a reason to come
// back, short enough that a session always has something on it.
inline constexpr aether::F64 kRefreshSeconds = 1800.0;

class OrdersSystem final : public runtime::SimSystem {
 public:
  void Step(runtime::StepContext& ctx, const runtime::EventList& in,
            runtime::EventList& out) override;
};

}  // namespace hearthfield::orders
