#include "hf/view/hud.hpp"

#include <algorithm>
#include <string>
#include <string_view>

#include "aether/ui/layout.hpp"
#include "hf/runtime/tick.hpp"
#include "hf/view/palette.hpp"

namespace hearthfield::view {
namespace {

using namespace aether;

// Between two status chips. Enough that a red warning does not read as part of
// the number before it.
constexpr F32 kChipGap = 22.0f;

}  // namespace

HudLayout LayoutHud(Vec2 canvas) {
  constexpr F32 kOneRow = kTouchTarget + kHudPad;
  // Enough for the chip block — the bar, its caption and the padding under it —
  // clear of the tab row above.
  constexpr F32 kChipRow = 44.0f;

  // The card is inset from the window on three sides, so its corners are real
  // corners rather than notches against the frame.
  const bool two_rows = canvas.x - kTabStrip < kChipsWant;
  const ui::Rect card{.x = kHudPad,
                      .y = kHudPad,
                      .w = canvas.x - (2.0f * kHudPad),
                      .h = two_rows ? kOneRow + kChipRow : kOneRow};

  // Everything below is measured from the CARD, never from the canvas. That is
  // the whole reason this returns a struct: the previous version had app/
  // computing the tab position from `canvas.x` while view/ computed the chip
  // limit from `kTabStrip`, and the two disagreed.
  const F32 inner_right = card.x + card.w - kHudPad;
  const F32 strip_w = (3.0f * kTabWidth) + (2.0f * kTabGap);
  const ui::Rect tabs{.x = inner_right - strip_w,
                      .y = card.y + (kHudPad * 0.5f),
                      .w = strip_w,
                      .h = kTouchTarget};
  // One row: beside the tabs, and the row ENDS where the strip begins — which
  // is the fact the chip cursor has to respect and used to be told separately.
  // Two: their own row underneath at the full inner width, which is what stops
  // a phone dropping the coop warning.
  const F32 chip_x = card.x + kHudPad;
  const F32 chip_right = two_rows ? inner_right : tabs.x - kHudPad;
  return HudLayout{.card = card,
                   .chips = ui::Rect{.x = chip_x,
                                     .y = two_rows ? card.y + kOneRow
                                                   : card.y + (kHudPad * 0.5f),
                                     .w = chip_right - chip_x,
                                     .h = kTouchTarget},
                   .tabs = tabs};
}

void DrawHud(ui::Context& ui, const runtime::ViewSnapshot& snapshot,
             const resources::Texture* paper) {
  const Vec2 canvas = ui.Canvas();
  constexpr F32 kBarHeight = 10.0f;
  const HudLayout layout = LayoutHud(canvas);

  // A CARD, NOT A SLAB. It was a full-bleed rectangle flush to the top, left
  // and right edges — the one shape the rounded corner language cannot express,
  // because a rounded corner against a window edge reads as a notch rather than
  // as a corner. Inset by one step and all four corners are real, with the
  // world running behind it on every side.
  if (paper == nullptr) {
    ui.Panel(layout.card, kPaper);
  } else {
    ui.NineSlice(layout.card, paper->Handle(), paper->Width(), paper->Height(),
                 ui::NineSliceMargins{.left = kNineSliceMargin,
                                      .right = kNineSliceMargin,
                                      .top = kNineSliceMargin,
                                      .bottom = kNineSliceMargin});
  }

  // EVERYTHING FITS INSIDE THIS, and the limit is what the tab strip leaves —
  // or the whole width, when the chips have a row of their own.
  // The old layout advanced by FIXED amounts (130, 175, 110, 110, 120) summing
  // past the tabs below about 1020 wide, so the last chips were drawn beneath
  // the buttons and simply disappeared — at 800x450 the coop warning and the
  // weather were both gone. Measuring each chip and stopping at the limit makes
  // a narrow window drop the LEAST important one instead of whichever happened
  // to be last; giving them their own row on a narrow canvas means nothing is
  // dropped at all.
  //
  // The cursor is a `ui::Stack` rather than an `x` this function advances: the
  // hand-rolled version had to remember to add the gap after a chip and to
  // check the limit before one, and the fixed-advance bug above is what
  // forgetting looks like.
  ui::Stack row{layout.chips, ui::Axis::kHorizontal, kChipGap};

  // One chip: drawn only if it fits whole, gap included, so two never touch and
  // none is ever half-visible under a button.
  const auto chip = [&](std::string_view text, Vec4 colour) {
    const F32 w = ui.MeasureText(text).x;
    if (!row.Fits(w)) {
      return;
    }
    const ui::Rect at = row.Take(w);
    ui.Label(Vec2{at.x, at.y}, text, colour);
  };

  chip(std::to_string(snapshot.coin) + " coin", kInk);

  // The barn as a BAR rather than a number: "how close am I to losing a
  // harvest" is the question, and 41/50 answers it more slowly than a bar that
  // is nearly full and turns red.
  //
  // The bar and its caption are ONE chip sized by the wider of the two, so the
  // bar can never appear without the number that explains it.
  const std::string barn = "barn " + std::to_string(snapshot.barn_total) + "/" +
                           std::to_string(snapshot.barn_capacity);
  const F32 barn_w = std::max(ui.MeasureText(barn).x, 150.0f);
  if (row.Fits(barn_w)) {
    const F32 fill =
        snapshot.barn_capacity == 0
            ? 0.0f
            : std::min(1.0f, static_cast<F32>(snapshot.barn_total) /
                                 static_cast<F32>(snapshot.barn_capacity));
    const ui::Rect at = row.Take(barn_w);
    const ui::Rect track{
        .x = at.x, .y = at.y + 4.0f, .w = barn_w, .h = kBarHeight};
    ui.Panel(track, kBarEmpty);
    ui.Panel(
        ui::Rect{.x = track.x, .y = track.y, .w = track.w * fill, .h = track.h},
        fill >= 1.0f ? kBarFull : kBarOk);
    ui.Label(Vec2{at.x, at.y + kBarHeight + 6.0f}, barn, kInk);
  }

  // ORDERED BY WHAT THE PLAYER CAME BACK TO DO, because that is the order a
  // narrow window drops them in. `ready` is why the game was opened, the coop
  // warning is the thing that needs fixing, and milling and weather are colour.
  if (snapshot.ready_count > 0) {
    chip(std::to_string(snapshot.ready_count) + " ready", kInk);
  }

  // The coop reads as a WARNING when it is empty rather than as a number.
  // "fed 43m" is information; "COOP EMPTY" is the thing the player came back to
  // fix, and it is the only red text on this bar. It was also the chip the old
  // layout lost first, which is precisely the wrong one to lose.
  if (snapshot.animals > 0) {
    if (snapshot.coop_fed) {
      const auto minutes = static_cast<U32>(snapshot.fed_ticks /
                                            (60ULL * runtime::kTicksPerSecond));
      chip("fed " + std::to_string(minutes) + "m", kInk);
    } else {
      chip("COOP EMPTY", kBarFull);
    }
  }
  if (snapshot.milling > 0) {
    chip("milling " + std::to_string(snapshot.milling), kInk);
  }
  if (snapshot.raining) {
    chip("rain", kRainInk);
  }
}

}  // namespace hearthfield::view
