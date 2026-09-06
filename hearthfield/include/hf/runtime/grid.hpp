// Where a plot IS, and which plot a tap landed on.
//
// Pure geometry over the plot table, in `runtime` because three callers need
// it and none of them may own it: view/ places the meshes, app/ resolves the
// pointer, and the tests check the arithmetic with no device. It names no
// scene, no renderer and no viewport — a viewport belongs to whoever built the
// ray, which is app/.
#pragma once

#include <algorithm>
#include <optional>
#include <span>

#include "aether/core/math/geometry.hpp"
#include "aether/core/types.hpp"
#include "hf/content/farm.hpp"
#include "hf/runtime/farm.hpp"
#include "hf/runtime/snapshot.hpp"

namespace hearthfield::runtime {

// A square board centred on the origin, lying in the y = 0 plane.
struct Grid {
  aether::U32 columns = content::kDefaultColumns;
  aether::F32 cell = content::kCellSize;

  [[nodiscard]] constexpr aether::Usize Count() const {
    return static_cast<aether::Usize>(columns) * columns;
  }

  [[nodiscard]] constexpr bool Has(PlotId id) const { return id < Count(); }

  // The centre of a plot's TOP face is at y = kTileHeight; this is the centre
  // of its cell on the ground, which is what everything else offsets from.
  [[nodiscard]] constexpr aether::Vec3 CenterOf(PlotId id) const {
    const auto col = static_cast<aether::F32>(id % columns);
    const auto row = static_cast<aether::F32>(id / columns);
    const aether::F32 half = 0.5f * static_cast<aether::F32>(columns - 1);
    return aether::Vec3{(col - half) * cell, 0.0f, (row - half) * cell};
  }
};

// How tall the thing standing on a plot is — the tile alone, or the tile plus
// whatever has grown on it.
[[nodiscard]] inline aether::F32 PlotHeight(const PlotView& plot) {
  if (plot.state == PlotState::kEmpty) {
    return content::kTileHeight;
  }
  const aether::F32 grown =
      std::max(content::kMinGrowth, plot.growth) * content::kCropHeight;
  return content::kTileHeight + grown;
}

// The volume a tap must hit to select this plot: the cell's footprint, as tall
// as whatever stands on it.
//
// TALL, and that is the whole point. A grown crop stands above its plot and,
// under a tilted camera, covers the plot BEHIND it — so intersecting the ground
// plane and dividing by the cell size (O(1), exact, tempting) resolves a tap on
// the top of a ripe stalk to the wrong plot. The player taps a crop and
// something else happens.
[[nodiscard]] inline aether::AABB PickBoxOf(const Grid& grid, PlotId id,
                                            const PlotView& plot) {
  const aether::Vec3 centre = grid.CenterOf(id);
  const aether::F32 half = 0.5f * grid.cell;
  const aether::F32 height = PlotHeight(plot);
  return aether::AABB{
      .min = aether::Vec3{centre.x - half, 0.0f, centre.z - half},
      .max = aether::Vec3{centre.x + half, height, centre.z + half}};
}

// The plot a ray hits first, or nothing.
//
// A flat grid of non-overlapping boxes needs no broadphase, and `core` already
// exposes the slab test `scene::query3` would reach for — so this is a loop
// over `Raycast(ray, AABB)` rather than a Collider3D array, and `hf_runtime`
// needs no scene dependency at all. query3 earns its place at H3, when
// buildings have volumes that are not a regular grid.
//
// Ties break to the LOWER plot id, matching RaycastNearest's earlier-index
// rule: two boxes at the same t must resolve the same way every run or a
// replay stops reproducing.
[[nodiscard]] inline std::optional<PlotId> PickPlot(
    const Grid& grid, std::span<const PlotView> plots,
    const aether::Ray3& ray) {
  std::optional<PlotId> best;
  aether::F32 best_t = 0.0f;
  const aether::Usize count = std::min(plots.size(), grid.Count());
  for (aether::Usize i = 0; i < count; ++i) {
    const auto id = static_cast<PlotId>(i);
    const std::optional<aether::RayHit3> hit =
        aether::Raycast(ray, PickBoxOf(grid, id, plots[i]));
    if (!hit) {
      continue;
    }
    if (!best || hit->t < best_t) {
      best = id;
      best_t = hit->t;
    }
  }
  return best;
}

}  // namespace hearthfield::runtime
