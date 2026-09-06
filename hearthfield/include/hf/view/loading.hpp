// The loading screen: what is on the glass while Load's phases run.
//
// GEA has no chapter on this — §16.4.2's air lock exists to buy its ABSENCE,
// and hearthfield uses that for travel. This is for the one load an air lock
// cannot hide, the first, and for the platforms where it is long enough to read
// (docs/plans/2026-08-29-loading-screen.md).
//
// It is drawn from a fraction and a label and NOTHING else. The game's world is
// half-built while this runs — that is the whole point of the phase split — so
// anything it touched would be a state it might not be in yet.
#pragma once

#include <string_view>

#include "aether/core/types.hpp"
#include "aether/ui/context.hpp"

namespace hearthfield::view {

// `fraction` is clamped, so a caller need not. Drawn against the design canvas
// ui::Context was begun with, not the framebuffer.
void DrawLoading(aether::ui::Context& ui, aether::F32 fraction,
                 std::string_view label);

}  // namespace hearthfield::view
