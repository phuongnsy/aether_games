// The HUD: the numbers a player glances at, always on.
//
// GEA §1.6.8.4 splits the front end into the HUD, in-game menus, and an
// "in-game GUI, allowing the player to manipulate his or her character's
// inventory … or perform other complex in-game tasks". THIS IS THE FIRST ONE.
// The barn, shop and order board are the third, and they are screens in app/ —
// built on a stack, dismissable, and able to take the pointer. Building them as
// "more HUD" gets a panel that can do none of those things.
//
// Drawn from the ViewSnapshot and nothing else, like the rest of view/. It
// takes no input: the buttons that OPEN the screens are the screens' business,
// because opening one is a flow decision and flow lives in app/.
#pragma once

#include "aether/core/types.hpp"
#include "aether/resources/texture.hpp"
#include "aether/ui/context.hpp"
#include "hf/runtime/snapshot.hpp"

namespace hearthfield::view {

// The smallest thing a finger may be asked to hit, in design units. Stated once
// and derived from, rather than each screen choosing a row height that felt
// right under a mouse — check 7 says "usable by touch", and a 20-unit row is
// comfortable to click and impossible to tap.
inline constexpr aether::F32 kTouchTarget = 44.0f;

// 16, ON THE SPACING SCALE (ui-direction §3). It was 12 — off the 4/8/16/24
// set the direction defines, which made that document aspirational the moment
// it was written. Everything here derives from it, so one value moves the card,
// the chips and the tab strip together.
inline constexpr aether::F32 kHudPad = 16.0f;

// WHAT THE BUTTON STRIP ON THE RIGHT COSTS, so the HUD can lay itself out in
// what is left. Stated here rather than in app/ because the HUD is the one that
// has to avoid it — three tabs, their gaps, and a margin.
//
// This exists because the HUD used to flow left with FIXED pixel advances that
// summed to ~700, while the tabs are anchored right: at 1280 it just fitted,
// and below about 1020 the coop warning and the weather were drawn UNDER the
// buttons and simply vanished. Nobody saw it because every capture until
// 2026-08-29 was 1280 wide.
inline constexpr aether::F32 kTabWidth = 100.0f;
inline constexpr aether::F32 kTabGap = 6.0f;
inline constexpr aether::F32 kTabStrip =
    (3.0f * (kTabWidth + kTabGap)) + kHudPad;

// Roughly what the chips want when every one of them is showing. An ESTIMATE —
// the chips are measured text and this is not — but it only decides the layout,
// and the chip loop still checks each one, so being a little wrong costs a row
// of whitespace rather than a clipped warning.
inline constexpr aether::F32 kChipsWant = 530.0f;

// WHERE THE HUD PUTS ITSELF, for a canvas of this size.
//
// Two rows on a narrow screen, and that is about PORTRAIT rather than about
// small windows: at 720x1280 the tab strip is 330 of 720 px, so the chips get
// 390 and the coop warning — the thing hud.cpp calls "precisely the wrong one
// to lose" — was dropped on Android's native orientation. Sharing one row is a
// desktop assumption; a phone has height to spare and no width.
// ONE SOURCE FOR THE CARD, THE CHIPS AND THE TABS. The tab strip is drawn by
// app/ (opening a screen is flow) and the chips by view/, and they have already
// drifted apart once — the HUD reserved a different width than the strip
// actually took, and chips vanished under the buttons. Both now read the same
// struct, so a change to one cannot silently miss the other.
// ROWS, NOT COORDINATES. This was `card` plus chip_x / chip_y / chip_limit /
// tabs — three scalars that had to agree about one row, and a Vec2 that a
// caller then advanced by hand. Both consumers now hand their row straight to a
// `ui::Stack`, which is the thing that actually stops a chip landing under a
// button (docs/plans/2026-08-31-ui-layout-stack.md).
struct HudLayout {
  aether::ui::Rect card;   // the panel, inset from the window edges
  aether::ui::Rect chips;  // the row the status chips flow along, left to right
  aether::ui::Rect tabs;   // the strip the three tab buttons flow along
};

[[nodiscard]] HudLayout LayoutHud(aether::Vec2 canvas);

// `paper` is the rounded 9-slice; NULL falls back to the flat fill it drew
// before, like every other surface in this game.
void DrawHud(aether::ui::Context& ui, const runtime::ViewSnapshot& snapshot,
             const aether::resources::Texture* paper);

}  // namespace hearthfield::view
