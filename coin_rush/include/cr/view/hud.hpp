// The in-play HUD, drawn from a plain data snapshot so it needn't reach into
// the game — the seam that keeps HUD layout separate from game state.
#pragma once

#include "cr/runtime/view_snapshot.hpp"  // PlayHud (a ViewSnapshot member)

namespace aether::ui {
class Context;
}

namespace game {

// Draw the coins/time/level labels from the snapshot's HUD data; `ui` must
// already be inside a frame.
void DrawPlayHud(aether::ui::Context& ui, const PlayHud& hud);

}  // namespace game
