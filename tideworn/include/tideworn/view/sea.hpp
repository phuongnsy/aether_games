// Snapshot -> renderer water state. The ONE place the sim's wind becomes a
// sea, so every consumer (geometry, foam, roughness, buoyancy) stays coherent.
#pragma once

#include "aether/render/water.hpp"
#include "tideworn/runtime/snapshot.hpp"

namespace aether::render {
class Renderer;
}

namespace tideworn::view {

// The wave field the snapshot implies. Exposed because buoyancy probes must
// sample the SAME sea the shader draws (render/water.hpp's whole premise).
[[nodiscard]] aether::render::WaterWaves SeaWaves(
    const runtime::ViewSnapshot& snapshot);

// Idempotent per frame. `plane_y` is the sea level the whole game agrees on.
void ApplySeaState(aether::render::Renderer& renderer,
                   const runtime::ViewSnapshot& snapshot, aether::F32 plane_y);

}  // namespace tideworn::view
