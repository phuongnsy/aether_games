#include "hf/features/plots/system.hpp"

#include <span>

#include "hf/content/crops.hpp"
#include "hf/runtime/farm.hpp"
#include "hf/runtime/tick.hpp"
#include "hf/runtime/world_view.hpp"

namespace hearthfield::plots {
namespace {

using runtime::Plot;
using runtime::PlotState;
using runtime::Tick;

}  // namespace

void PlotsSystem::Step(runtime::StepContext& ctx,
                       const runtime::EventList& /*in*/,
                       runtime::EventList& out) {
  const Tick now = ctx.world.Now();
  const std::span<Plot> plots = ctx.world.Plots();

  // One tap does whatever the plot under it invites — sow an empty one,
  // harvest a ready one. The genre's whole interaction, and the reason
  // `hovered` is resolved in app/ rather than here (a sim knows no viewport).
  // LOCKED LAND IS NOT DERIVED FROM THE PLOT, it is derived from how much of
  // the board the player owns — plots [0, unlocked) are theirs. Kept as one
  // number in the purse rather than a flag per plot so the two cannot disagree,
  // and so a v1 save can be migrated by setting it (H3 plan §3e).
  const bool owned = ctx.input.hovered < ctx.world.TheLand().owned;
  // WHAT to sow comes from the input too (H4): the seed choice is a player
  // action, so it belongs in the recorded stream rather than in a member of
  // this system that a replay would never see.
  const content::CropId selected = ctx.input.sow_crop;
  if (ctx.input.tap && ctx.world.HasPlot(ctx.input.hovered) && owned &&
      content::CropExists(selected)) {
    Plot& plot = plots[ctx.input.hovered];
    if (plot.state == PlotState::kEmpty) {
      plot.state = PlotState::kGrowing;
      plot.crop = selected;
      plot.sown_tick = now;
      plot.ready_tick = now + runtime::TicksFromSeconds(
                                  content::CropById(selected).grow_seconds);
      out.emplace_back(
          runtime::Sown{.plot = ctx.input.hovered, .crop = selected});
    } else if (plot.state == PlotState::kReady) {
      out.emplace_back(
          runtime::Harvested{.plot = ctx.input.hovered,
                             .crop = plot.crop,
                             .amount = content::CropById(plot.crop).yield});
      plot = Plot{};
    }
  }

  // THE CLOSED-FORM CATCH-UP, and it looks like nothing because that is the
  // finding: growth is monotone and per-plot, so an absence is the ordinary
  // readiness scan run against a `now` that jumped (spec §4.3). A feature that
  // couples across entities cannot do this, and the equivalence test is what
  // will say so.
  for (aether::Usize i = 0; i < plots.size(); ++i) {
    Plot& plot = plots[i];
    if (plot.state == PlotState::kGrowing && now >= plot.ready_tick) {
      plot.state = PlotState::kReady;
      out.emplace_back(runtime::CropReady{
          .plot = static_cast<runtime::PlotId>(i), .crop = plot.crop});
    }
  }
}

}  // namespace hearthfield::plots
