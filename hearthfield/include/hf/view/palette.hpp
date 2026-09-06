// The game's UI colours, in ONE place.
//
// They were in two: `view/hud.cpp` and `app/screens.cpp` each defined their own
// `kInk` and `kPaper` with the same literals, which is exactly how the
// tab-strip width came to disagree with the space the HUD reserved for it. A
// palette that lives twice drifts once.
//
// DERIVED FROM THE ART BIBLE (docs/specs/2026-08-25-hearthfield-art-bible.md),
// not chosen here. Its brief for every object in this world is "warm saturated
// palette, matte surfaces" and "clean flat colour blocking, soft rounded edges,
// no gloss", and it authors the world in grass #A2B18B, soil #8A6A47, timber
// #B59E81 and cream #E5DFD4. The UI had been rendering with `ui::Theme`'s
// engine defaults — a cool purple-blue #47_4A_78 — which is the one thing on
// screen that belongs to a different game.
#pragma once

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "aether/ui/context.hpp"

namespace hearthfield::view {

// Hex to linear-ish Vec4, so the constants below read as the art bible writes
// them rather than as six-decimal triples nobody can compare to a spec.
[[nodiscard]] constexpr aether::Vec4 Rgb(aether::U32 hex,
                                         aether::F32 a = 1.0f) {
  return aether::Vec4{static_cast<aether::F32>((hex >> 16) & 0xFF) / 255.0f,
                      static_cast<aether::F32>((hex >> 8) & 0xFF) / 255.0f,
                      static_cast<aether::F32>(hex & 0xFF) / 255.0f, a};
}

// --- surfaces ---------------------------------------------------------------
// The established cream, kept: it is what the HUD and every screen have been
// drawn on since H5 and it already sits in the art bible's family.
inline constexpr aether::Vec4 kPaper = Rgb(0xF5EBCC, 0.97f);
inline constexpr aether::Vec4 kScrim = Rgb(0x000000, 0.45f);
// Alternating list rows, a shade apart so a long barn list is scannable.
inline constexpr aether::Vec4 kRowA = Rgb(0xE6DCBD);
inline constexpr aether::Vec4 kRowB = Rgb(0xEDE4C7);

// --- text -------------------------------------------------------------------
// Deep timber rather than black: the art bible has no black in it, and true
// black on cream reads as a spreadsheet.
inline constexpr aether::Vec4 kInk = Rgb(0x241F17);
inline constexpr aether::Vec4 kDim = Rgb(0x736B5C);
// On a soil-coloured button, so it is the cream rather than the ink.
inline constexpr aether::Vec4 kOnButton = Rgb(0xF7F0DC);

// --- buttons ----------------------------------------------------------------
// The art bible's soil, one step darker for the press and one lighter for the
// hover. Soil rather than timber #B59E81 because a button needs to hold cream
// text at HUD size, and timber is too light to carry it.
inline constexpr aether::Vec4 kButton = Rgb(0x8A6A47);
inline constexpr aether::Vec4 kButtonHover = Rgb(0xA5825C);
inline constexpr aether::Vec4 kButtonActive = Rgb(0x6B4F35);

// --- the HUD's own signals --------------------------------------------------
// A bar that is filling, a bar that is FULL (a harvest about to be lost), and
// the weather. The full state is the art bible's warm red rather than a pure
// alarm red, for the same reason the ink is not black.
inline constexpr aether::Vec4 kBarEmpty = Rgb(0xB8A884);
inline constexpr aether::Vec4 kBarOk = Rgb(0x6B953D);
inline constexpr aether::Vec4 kBarFull = Rgb(0xC7563D);
inline constexpr aether::Vec4 kRainInk = Rgb(0x52698F);

// --- accents ----------------------------------------------------------------
// ONE PER SCREEN, and only one. The art bible allows "a saturated accent once
// per building as the thing that identifies it at a glance"; a screen is the
// same kind of object, so each gets exactly one and it marks the verb the
// screen exists for. A second accent anywhere means one of them is wrong.
inline constexpr aether::Vec4 kAccentBarn = Rgb(0x6B953D);    // feeding
inline constexpr aether::Vec4 kAccentShop = Rgb(0xC79A3D);    // the planting
inline constexpr aether::Vec4 kAccentOrders = Rgb(0xC7563D);  // a ready order

// --- spacing ----------------------------------------------------------------
// ONE UNIT, FOUR MULTIPLES (ui-direction §3). It was 16 almost everywhere with
// a stray 18, a 60 and a 4 where something needed nudging — a habit rather than
// a rhythm. A layout number outside this set is a bug or a new entry, not a
// nudge.
inline constexpr aether::F32 kTight = 4.0f;
inline constexpr aether::F32 kSnug = 8.0f;
inline constexpr aether::F32 kPad = 16.0f;
inline constexpr aether::F32 kGap = 24.0f;

// --- type -------------------------------------------------------------------
// TWO STEPS, because a bitmap font has no third. The face is 5x7 baked at x2
// and scales by INTEGERS or it blurs — a 1.5x title is precisely the defect
// fixed hours earlier, when a 1 px outline turned out to be two thirds of every
// stroke. Emphasis below the title is colour, never size.
inline constexpr aether::F32 kTitleScale = 2.0f;
inline constexpr aether::F32 kBodyScale = 1.0f;

// The 9-slice inset, in texture pixels — `slices` in panel.json and btn.json,
// which both generators emit as `radius + border`. Stated once here because the
// panel and the button skin must agree with the ART, and a margin that
// disagrees stretches a corner instead of pinning it.
inline constexpr aether::F32 kNineSliceMargin = 8.0f;

// What the engine's widgets shade themselves with. `text` is the BUTTON label
// colour (and the icon tint) — a `Label` always carries its own, which is why
// the ink above is not in here.
[[nodiscard]] inline aether::ui::Theme Theme() {
  return aether::ui::Theme{.panel = kPaper,
                           .button = kButton,
                           .button_hover = kButtonHover,
                           .button_active = kButtonActive,
                           .text = kOnButton};
}

}  // namespace hearthfield::view
