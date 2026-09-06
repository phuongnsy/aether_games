#include "screens.hpp"

#include <cmath>
#include <string>

#include "hf/content/animals.hpp"
#include "hf/content/items.hpp"
#include "hf/content/recipes.hpp"
#include "hf/features/economy/system.hpp"
#include "hf/view/hud.hpp"
#include "hf/view/palette.hpp"

namespace hearthfield::app {
namespace {

using namespace aether;
using view::kAccentBarn;
using view::kAccentOrders;
using view::kAccentShop;
using view::kDim;
using view::kGap;
using view::kInk;
using view::kPad;
using view::kPaper;
using view::kRowA;
using view::kRowB;
using view::kScrim;
using view::kSnug;
using view::kTight;
using view::kTitleScale;
using view::kTouchTarget;

// The title bar holds a x2 title (28 units) plus padding above and below.
constexpr F32 kTitleH = 48.0f;

// ONE WIDGET IN THE SCREEN'S ACCENT — a ring behind it, not a recolour.
//
// The art bible allows a saturated accent "once per building as the thing that
// identifies it at a glance"; a screen is the same kind of object, so each has
// exactly one and it marks the verb the screen exists for (ui-direction §4).
//
// A RING BECAUSE A SKINNED BUTTON CANNOT BE TINTED. The first version swapped
// `ui::Theme.button` around the widget, which does nothing once
// `SetButtonSkin` is set: `DrawButtonBg` draws the skin's 9-slice and never
// consults the theme (engine/ui/src/context.cpp). The alternatives were three
// more generated skins — nine PNGs to express three colours — or an engine
// tint parameter, and a Panel drawn one step larger costs neither.
void AccentRing(ui::Context& ui, const ui::Rect& rect, Vec4 colour) {
  ui.Panel(ui::Rect{.x = rect.x - kTight,
                    .y = rect.y - kTight,
                    .w = rect.w + (2.0f * kTight),
                    .h = rect.h + (2.0f * kTight)},
           colour);
}

// The paper surface: the rounded 9-slice when the art loaded, a flat fill when
// it did not. One place, so every screen rounds the same way.
void PaperPanel(ui::Context& ui, const ui::Rect& rect,
                const resources::Texture* art) {
  if (art == nullptr) {
    ui.Panel(rect, kPaper);
    return;
  }
  ui.NineSlice(rect, art->Handle(), art->Width(), art->Height(),
               ui::NineSliceMargins{.left = view::kNineSliceMargin,
                                    .right = view::kNineSliceMargin,
                                    .top = view::kNineSliceMargin,
                                    .bottom = view::kNineSliceMargin});
}

// The panel every screen sits in, and the title bar with its close button.
// Returns the content rect below the title. `closed` is set when the player
// dismissed it.
//
// `wanted_h` is what THIS screen's content needs; the panel takes the smaller
// of that and the canvas. A screen whose content grows past the window still
// scrolls — this only stops it being cropped while there was room.
[[nodiscard]] ui::Rect Frame(ui::Context& ui, const ScreenCtx& ctx,
                             std::string_view title, bool& closed,
                             F32 wanted_h = 420.0f) {
  const Vec2 canvas = ui.Canvas();
  const ui::Rect full{.x = 0.0f, .y = 0.0f, .w = canvas.x, .h = canvas.y};
  ui.Panel(full, kScrim);
  // THE SCRIM MUST CLAIM THE POINTER, not merely darken what is behind it.
  // `WantsPointer` is true only over a WIDGET rect, and a Panel is not one — so
  // until 2026-08-29 a drag anywhere outside the panel's buttons fell straight
  // through and PANNED THE BOARD under an open screen. It looked modal and was
  // not. `BlockPointer` over the whole canvas is what the engine provides for
  // exactly this, and the scrim's own rect is the honest region to claim: what
  // the dimming covers is what the screen is holding.
  ui.BlockPointer(full);

  const F32 w = std::min(560.0f, canvas.x - (2.0f * kPad));
  const F32 h = std::min(wanted_h, canvas.y - (2.0f * kPad));
  const ui::Rect panel{
      .x = (canvas.x - w) * 0.5f, .y = (canvas.y - h) * 0.5f, .w = w, .h = h};
  PaperPanel(ui, panel, ctx.panel);

  // THE TITLE IS THE ONE THING AT x2 (ui-direction §2). A bitmap font scales by
  // integers or it blurs, so the whole type scale is two steps and this is the
  // upper one; everything below it separates by COLOUR instead. Restored to the
  // body scale immediately, because `text_scale_` is context state and a screen
  // that leaked it would render its own contents at title size.
  ui.SetTextScale(kTitleScale);
  ui.Label(Vec2{panel.x + kPad, panel.y + kPad}, title, kInk);
  ui.SetTextScale(view::kBodyScale);

  // A close button at the touch minimum, not at the size of an X glyph.
  if (ui.Button(ui::Rect{.x = panel.x + panel.w - kTouchTarget - kPad,
                         .y = panel.y + kSnug,
                         .w = kTouchTarget,
                         .h = kTouchTarget},
                "X")) {
    closed = true;
  }
  return ui::Rect{.x = panel.x + kPad,
                  .y = panel.y + kTitleH + kPad,
                  .w = panel.w - (2.0f * kPad),
                  .h = panel.h - kTitleH - (2.0f * kPad)};
}

}  // namespace

// ---- barn -------------------------------------------------------------------

void BarnScreen::HandleInput(ScreenCtx& ctx) {
  if (ctx.close_requested) {
    Stack().Pop();
  }
}

void BarnScreen::BuildUi(ScreenCtx& ctx, ui::Context& ui) {
  bool closed = false;
  const ui::Rect body = Frame(ui, ctx, "Barn", closed);
  if (closed) {
    Stack().Pop();
  }
  const runtime::ViewSnapshot& snap = *ctx.snapshot;

  // The feed button eats the bottom of the panel, so the list scrolls above it.
  const ui::Rect list{
      .x = body.x, .y = body.y, .w = body.w, .h = body.h - kTouchTarget - kPad};

  const Usize rows = content::kItemCount;
  const F32 row_h = kTouchTarget;
  const F32 offset =
      ui.BeginScrollView("barn-list", list, static_cast<F32>(rows) * row_h);

  // CULLED, not merely clipped. ui's own guide: "clipping HIDES a row, it does
  // not make it free". The item catalogue is append-only, so this list only
  // ever grows.
  const ui::Context::RowRange visible =
      ui::Context::VisibleRows(offset, list.h, row_h, rows);
  for (Usize i = visible.first; i < visible.last; ++i) {
    const F32 y = list.y + (static_cast<F32>(i) * row_h) - offset;
    const ui::Rect row{.x = list.x, .y = y, .w = list.w, .h = row_h - 2.0f};
    ui.Panel(row, i % 2 == 0 ? kRowA : kRowB);
    const U32 count = snap.items[i];
    ui.Label(
        Vec2{row.x + kPad, row.y + kTight + kSnug},
        std::string(content::ItemById(static_cast<content::ItemId>(i)).name),
        count > 0 ? kInk : kDim);
    ui.Label(Vec2{row.x + row.w - kGap - kPad, row.y + kTight + kSnug},
             std::to_string(count), count > 0 ? kInk : kDim);
  }
  ui.EndScrollView();

  // FEEDING LIVES HERE rather than in the shop, and the placement is the point:
  // this is the panel where the player is looking at their wheat, so spending
  // some of it on the birds is the obvious next thing. It is not a purchase, so
  // it does not belong among the prices.
  const bool enough = snap.items[content::kFeedItem] >= content::kFeedPerFill;
  const ui::Rect feed{.x = body.x,
                      .y = body.y + body.h - kTouchTarget,
                      .w = body.w,
                      .h = kTouchTarget};
  const std::string label =
      "Feed the coop  (" + std::to_string(content::kFeedPerFill) + " " +
      std::string(content::ItemById(content::kFeedItem).name) + ")";
  // Says whether it CAN be done rather than letting the player press it and be
  // refused — the same rule the order board's Ship follows. The refusal still
  // exists in the sim; this only stops the UI inviting it.
  // ONLY WHEN IT CAN BE PRESSED. An accent marks the verb the screen is FOR,
  // and ringing a refusal invites exactly the press the label two lines below
  // exists to avoid.
  if (enough) {
    AccentRing(ui, feed, kAccentBarn);
  }
  if (ui.Button(feed, enough ? label : "not enough feed") && enough) {
    ctx.api.Feed();
  }
}

// ---- shop -------------------------------------------------------------------

void ShopScreen::HandleInput(ScreenCtx& ctx) {
  if (ctx.close_requested) {
    Stack().Pop();
  }
}

void ShopScreen::BuildUi(ScreenCtx& ctx, ui::Context& ui) {
  // A GRID: two upgrades, then one seed packet per crop. It grows with the
  // catalogue, which is why it is a grid and scrolls rather than two lonely
  // tiles.
  constexpr Usize kCols = 2;
  const F32 tile_h = kTouchTarget * 1.6f;
  // The islands come last, after the upgrades and the seed packets: an island
  // is the largest thing on this shelf and the one a player reaches for least
  // often. Every island gets a tile INCLUDING the one being stood on, because a
  // list that hides your own position reads as having lost it.
  const Usize tiles = 2 + content::kCrops.size() + content::kIslands.size();
  const auto grid_rows = static_cast<Usize>((tiles + kCols - 1) / kCols);
  // THE SEED PICKER IS FIXED TO THE BOTTOM and the grid must stop above it.
  const F32 picker_strip = kTouchTarget + kPad + kPad;

  // SIZED TO ITS CONTENT, because a fixed 420 cropped the last row. Seven tiles
  // is four rows needing 329.6 px against a 270 px grid, so `far isle` drew as
  // a 10.8 px SLIVER of a 70.4 px tile — with no scrollbar, an invisible
  // destination and a stray bar that reads as a rendering fault. Growing the
  // panel is the fix rather than snapping the grid to whole rows, which would
  // have hidden the tile completely.
  const F32 rows_h = (static_cast<F32>(grid_rows) * (tile_h + kPad)) - kPad;
  const F32 wanted = kTitleH + (2.0f * kPad) + rows_h + picker_strip;

  bool closed = false;
  const ui::Rect body = Frame(ui, ctx, "Shop", closed, wanted);
  if (closed) {
    Stack().Pop();
  }
  const runtime::ViewSnapshot& snap = *ctx.snapshot;
  const F32 tile_w = (body.w - kPad) / static_cast<F32>(kCols);
  // THE SCROLL VIEW STOPS ABOVE THE PICKER, which is fixed to the panel's
  // bottom. It used to be handed the whole body and got away with it while the
  // grid was two rows; the islands made it four, and the last tile drew
  // straight through the "planting:" row. A scroll region that overlaps a fixed
  // control is a layout bug that only appears once the content grows — and
  // clipping it MOVED that bug rather than removing it, which is what the
  // content-sized panel above finally closes. The scroll stays for the window
  // too short to hold the panel, which is now the only case that crops.
  // SNAPPED TO WHOLE ROWS, because a partial one is unreadable and unclickable.
  // The content-sized panel above means this only bites on a window too SHORT
  // to hold the panel — and the lint caught exactly that at 800x450, where the
  // last tile survived as 3 px of 70. Snapping was rejected when the panel was
  // a fixed 420 (it would have hidden `far isle` outright); with the panel now
  // growing when there is room, the only thing it hides is a row you scroll to.
  const F32 room = std::max(body.h - picker_strip, tile_h);
  const F32 pitch = tile_h + kPad;
  const auto whole = std::max(1.0f, std::floor((room + kPad) / pitch));
  const ui::Rect grid{.x = body.x,
                      .y = body.y,
                      .w = body.w,
                      .h = std::min(room, (whole * pitch) - kPad)};
  const F32 offset = ui.BeginScrollView(
      "shop-grid", grid, static_cast<F32>(grid_rows) * (tile_h + kPad));

  const auto tile_rect = [&](Usize i) {
    const auto col = static_cast<F32>(i % kCols);
    const auto row = static_cast<F32>(i / kCols);
    return ui::Rect{.x = body.x + (col * (tile_w + kPad * 0.5f)),
                    .y = body.y + (row * (tile_h + kPad)) - offset,
                    .w = tile_w - (kPad * 0.5f),
                    .h = tile_h};
  };

  const U32 land = economy::LandPrice(snap.unlocked);
  const bool more_land = snap.unlocked < snap.plots.size();
  if (ui.Button(tile_rect(0), more_land ? "Land  " + std::to_string(land)
                                        : "Land  (all yours)") &&
      more_land) {
    ctx.api.BuyLand();
  }

  const U32 barn = economy::BarnPrice(snap.barn_capacity);
  if (ui.Button(tile_rect(1), "Barn +" + std::to_string(economy::kBarnStep) +
                                  "  " + std::to_string(barn))) {
    ctx.api.BuyBarn();
  }

  for (Usize c = 0; c < content::kCrops.size(); ++c) {
    const content::Crop& crop = content::kCrops[c];
    const U32 price = economy::PacketPrice(economy::kPacketSize);
    if (ui.Button(tile_rect(2 + c), std::string(crop.name) + " x" +
                                        std::to_string(economy::kPacketSize) +
                                        "  " + std::to_string(price))) {
      ctx.api.BuyItem(crop.yields, economy::kPacketSize);
    }
  }

  // ONE TILE PER ISLAND, and its label is its state — there is no room here for
  // a second line, and a price beside an island you already own would read as a
  // second charge. Three states: where you are, somewhere you own (go), and
  // somewhere you do not (the price).
  const Usize islands_at = 2 + content::kCrops.size();
  for (Usize i = 0; i < content::kIslands.size(); ++i) {
    const auto id = static_cast<content::IslandId>(i);
    const content::Island& isle = content::kIslands[i];
    const bool owned = (snap.islands_unlocked & (1u << id)) != 0;
    const bool here = snap.island == id;
    const std::string label =
        here ? std::string(isle.name) + "  (here)"
             : (owned ? "Go to " + std::string(isle.name)
                      : std::string(isle.name) + "  " +
                            std::to_string(isle.unlock_coins));
    if (ui.Button(tile_rect(islands_at + i), label) && !here) {
      // The screen sends ONE verb and the sim decides: buying is refused for
      // want of coin, travel for want of the island. Neither is checked here,
      // because a button that pre-judged would be a second copy of the rule.
      if (owned) {
        ctx.api.TravelTo(id);
        // Out of the way: the crossing is the thing to watch, and a shop panel
        // over it would hide the one moment this feature exists for. BUYING
        // does not close it — the tile turns into "Go to" and the next tap is
        // the obvious one.
        Stack().Pop();
      } else {
        ctx.api.BuyIsland(id);
      }
    }
  }
  ui.EndScrollView();

  // The seed selector sits under the grid rather than in it: choosing what to
  // plant is not a purchase, and putting it among the prices reads as one.
  const F32 picker_y = body.y + body.h - kTouchTarget;
  ui.Label(Vec2{body.x, picker_y - kPad}, "planting:", kDim);
  for (Usize c = 0; c < content::kCrops.size(); ++c) {
    const auto crop = static_cast<content::CropId>(c);
    const bool chosen = ctx.api.SelectedCrop() == crop;
    const ui::Rect pick{
        .x = body.x + (static_cast<F32>(c) * (kTouchTarget * 2.5f + kSnug)),
        .y = picker_y,
        .w = kTouchTarget * 2.0f,
        .h = kTouchTarget};
    // THE CHOSEN one carries the accent, not both: it is the choice that
    // persists after the screen closes, which is what the screen is for.
    const std::string label = chosen
                                  ? "> " + std::string(content::kCrops[c].name)
                                  : std::string(content::kCrops[c].name);
    if (chosen) {
      AccentRing(ui, pick, kAccentShop);
    }
    if (ui.Button(pick, label)) {
      ctx.api.SelectCrop(crop);
    }
  }
}

// ---- orders -----------------------------------------------------------------

void OrdersScreen::HandleInput(ScreenCtx& ctx) {
  if (ctx.close_requested) {
    Stack().Pop();
  }
}

void OrdersScreen::BuildUi(ScreenCtx& ctx, ui::Context& ui) {
  bool closed = false;
  const ui::Rect body = Frame(ui, ctx, "Orders", closed);
  if (closed) {
    Stack().Pop();
  }
  const runtime::ViewSnapshot& snap = *ctx.snapshot;

  // AN EMPTY BOARD SAYS WHAT IT IS WAITING FOR — ONCE. Three rows reading
  // "(waiting)" told a player nothing about whether the game was working, how
  // long it takes, or what to do about it, and a fresh farm saw exactly that
  // three times. The first attempt at a fix put the explanation on every row
  // and it both repeated and overflowed; the hint belongs to the BOARD, not to
  // each slot.
  bool any_active = false;
  for (Usize i = 0; i < runtime::kOrderSlots; ++i) {
    any_active = any_active || snap.orders[i].active;
  }

  const F32 row_h = kTouchTarget * 1.3f;
  const F32 offset = ui.BeginScrollView(
      "order-list", body, static_cast<F32>(runtime::kOrderSlots) * row_h);
  const ui::Context::RowRange visible =
      ui::Context::VisibleRows(offset, body.h, row_h, runtime::kOrderSlots);

  for (Usize i = visible.first; i < visible.last; ++i) {
    const runtime::Order& order = snap.orders[i];
    const F32 y = body.y + (static_cast<F32>(i) * row_h) - offset;
    const ui::Rect row{.x = body.x, .y = y, .w = body.w, .h = row_h - kTight};
    ui.Panel(row, i % 2 == 0 ? kRowA : kRowB);
    if (!order.active) {
      ui.Label(Vec2{row.x + kPad, row.y + kPad}, "no order yet", kDim);
      continue;
    }
    ui.Label(Vec2{row.x + kPad, row.y + kPad},
             std::string(content::ItemById(order.item).name) + " x" +
                 std::to_string(order.count) + "   " +
                 std::to_string(order.reward) + " coin",
             kInk);
    // The button says whether it CAN be filled, rather than letting the player
    // press it and be refused. The refusal still exists in the sim — this only
    // stops the UI from inviting it.
    const bool can = snap.items[order.item] >= order.count;
    // A SHIPPABLE order is the accent; a short one is an ordinary button.
    const ui::Rect ship{.x = row.x + row.w - kTouchTarget * 2.2f,
                        .y = row.y + kSnug,
                        .w = kTouchTarget * 2.5f,
                        .h = kTouchTarget};
    if (can) {
      AccentRing(ui, ship, kAccentOrders);
    }
    if (ui.Button(ship, can ? "Ship" : "short") && can) {
      ctx.api.Fill(static_cast<U8>(i));
    }
  }
  ui.EndScrollView();

  if (!any_active) {
    ui.Label(
        Vec2{body.x,
             body.y + (static_cast<F32>(runtime::kOrderSlots) * row_h) + kPad},
        "harvest something and an order will arrive", kDim);
  }
}

}  // namespace hearthfield::app
