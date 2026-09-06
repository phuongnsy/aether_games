// The verb seam the screens drive the game through.
//
// The headline case is at the bottom, and it is the one that protects every
// other oracle in this game: a session built by CALLING GameApi must replay
// identically. If a verb ever reaches past the latch into the world, the replay
// diverges — while H0's replay test and H2's save-across-a-gap both stay green,
// because they only ever replay what was recorded.
#include <doctest/doctest.h>

#include <variant>
#include <vector>

#include "hf/content/items.hpp"
#include "hf/features/economy/system.hpp"
#include "hf/features/orders/system.hpp"
#include "hf/features/plots/system.hpp"
#include "hf/features/production/system.hpp"
#include "hf/runtime/game_api.hpp"
#include "hf/runtime/game_world.hpp"
#include "hf/runtime/replay.hpp"

using namespace aether;
using hearthfield::economy::EconomySystem;
using hearthfield::orders::OrdersSystem;
using hearthfield::plots::PlotsSystem;
using hearthfield::production::ProductionSystem;
using hearthfield::runtime::GameApi;
using hearthfield::runtime::GameWorld;
using hearthfield::runtime::LatchedInput;
using hearthfield::runtime::PlotState;
namespace content = hearthfield::content;
namespace economy = hearthfield::economy;
namespace runtime = hearthfield::runtime;

namespace {

constexpr F32 kDt = 1.0f / 60.0f;

// A farm plus the pending latch a screen would write into — the same shape
// app/ has, minus the window.
struct Bench {
  GameWorld world;
  PlotsSystem plots;
  ProductionSystem production;
  OrdersSystem orders;
  EconomySystem economy;
  LatchedInput pending;

  explicit Bench(Usize count = 9, U64 seed = 5) : world(seed, count) {
    world.World().SetBuildingCount(1);
    world.AddSystem(plots);
    world.AddSystem(production);
    world.AddSystem(orders);
    world.AddSystem(economy);
  }

  [[nodiscard]] GameApi Api() { return GameApi(pending); }
  [[nodiscard]] runtime::Barn& Barn() { return world.World().TheBarn(); }
  [[nodiscard]] runtime::Purse& ThePurse() { return world.World().ThePurse(); }
  [[nodiscard]] runtime::Land& TheLand() { return world.World().TheLand(); }

  // One frame: whatever the screens asked for, then clear the one-shots —
  // exactly what app/'s FixedUpdate does.
  const runtime::EventList& Step() {
    const runtime::EventList& events = world.Step(pending, kDt);
    pending.tap = false;
    pending.offline_ticks = 0;
    pending.queue_recipe = LatchedInput::kNoRecipe;
    pending.fill_slot = LatchedInput::kNoSlot;
    pending.buy_land = false;
    pending.buy_barn = false;
    pending.buy_item = LatchedInput::kNoItem;
    pending.buy_count = 0;
    return events;
  }
};

template <class E>
[[nodiscard]] bool Contains(const runtime::EventList& events) {
  for (const runtime::GameEvent& event : events) {
    if (std::holds_alternative<E>(event)) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("a barn upgrade costs coin, adds capacity, and gets dearer") {
  Bench bench;
  bench.ThePurse().coin = 500;
  const U32 first = economy::BarnPrice(bench.Barn().capacity);
  CHECK(first == economy::kBarnBasePrice);

  bench.Api().BuyBarn();
  bench.Step();
  CHECK(bench.Barn().capacity ==
        economy::kStartingCapacity + economy::kBarnStep);
  CHECK(bench.ThePurse().coin == 500 - first);

  const U32 second = economy::BarnPrice(bench.Barn().capacity);
  CHECK(second > first);

  SUBCASE("and refuses when the purse is short") {
    bench.ThePurse().coin = 0;
    const U32 was = bench.Barn().capacity;
    bench.Api().BuyBarn();
    CHECK(Contains<runtime::Refused>(bench.Step()));
    CHECK(bench.Barn().capacity == was);
  }
}

TEST_CASE("a seed packet is coin in, goods out") {
  Bench bench;
  bench.ThePurse().coin = 100;
  bench.Api().BuyItem(content::kCornItem, economy::kPacketSize);
  bench.Step();
  CHECK(bench.Barn().Of(content::kCornItem) == economy::kPacketSize);
  CHECK(bench.ThePurse().coin ==
        100 - economy::PacketPrice(economy::kPacketSize));

  SUBCASE("refused whole when the barn has no room for the whole packet") {
    // Not part-delivered: paying full price for half a packet because the barn
    // was nearly full is the kind of thing a player remembers. A harvest is
    // different — nobody chose for it to arrive.
    bench.Barn().capacity = bench.Barn().Total() + 1;
    const U32 coin = bench.ThePurse().coin;
    bench.Api().BuyItem(content::kWheatItem, economy::kPacketSize);
    CHECK(Contains<runtime::Refused>(bench.Step()));
    CHECK(bench.ThePurse().coin == coin);
    CHECK(bench.Barn().Of(content::kWheatItem) == 0);
  }
  SUBCASE("refused when the purse is short") {
    bench.ThePurse().coin = 0;
    bench.Api().BuyItem(content::kWheatItem, economy::kPacketSize);
    CHECK(Contains<runtime::Refused>(bench.Step()));
    CHECK(bench.Barn().Of(content::kWheatItem) == 0);
  }
}

TEST_CASE("SelectCrop decides what the next tap plants, and it is STICKY") {
  Bench bench;
  bench.TheLand().owned = 9;

  bench.Api().SelectCrop(content::kCorn);
  bench.pending.tap = true;
  bench.pending.hovered = 0;
  bench.Step();
  CHECK(bench.world.World().Plots()[0].crop == content::kCorn);

  // A mode, not an action: the choice survives the step that cleared every
  // one-shot verb, because the player chose it once and meant it.
  CHECK(bench.Api().SelectedCrop() == content::kCorn);
  bench.pending.tap = true;
  bench.pending.hovered = 1;
  bench.Step();
  CHECK(bench.world.World().Plots()[1].crop == content::kCorn);
}

TEST_CASE("Queue takes a RecipeId, so the mill is not two hardcoded bools") {
  Bench bench;
  bench.Barn().Add(content::kCornItem, 4);
  bench.Api().Queue(0, content::kGrindCorn);
  bench.Step();
  CHECK(bench.world.World().Buildings()[0].queued == 1);
  CHECK(bench.world.World().Buildings()[0].queue[0] == content::kGrindCorn);
}

TEST_CASE("A SESSION DRIVEN BY THE API REPLAYS IDENTICALLY") {
  // What §3(b) of the H4 plan is for. Every verb writes a latched field and
  // nothing else, so recording the latch is recording the session — and a verb
  // that reached into the world instead would leave this test comparing a live
  // run against a replay that never saw the action.
  runtime::ReplayRecorder recorder;
  U64 live = 0;
  {
    Bench bench;
    // FOUR of nine owned, so BuyLand below has something to buy — with the
    // whole board already owned it is refused, which is correct and would have
    // made this a test of nothing.
    bench.TheLand().owned = 4;
    bench.ThePurse().coin = 500;

    const auto grow = static_cast<U32>(runtime::TicksFromSeconds(
        content::CropById(content::kWheat).grow_seconds));

    // A scripted session, played entirely through the verbs a screen calls.
    bench.Api().SelectCrop(content::kWheat);
    bench.pending.tap = true;
    bench.pending.hovered = 0;
    recorder.Record(bench.pending);
    bench.Step();

    bench.Api().BuyItem(content::kWheatItem, economy::kPacketSize);
    recorder.Record(bench.pending);
    bench.Step();

    bench.Api().Queue(0, content::kGrindWheat);
    recorder.Record(bench.pending);
    bench.Step();

    bench.Api().BuyBarn();
    recorder.Record(bench.pending);
    bench.Step();

    bench.pending.offline_ticks = grow * 4;  // long enough to ripen and grind
    recorder.Record(bench.pending);
    bench.Step();

    bench.Api().BuyLand();
    recorder.Record(bench.pending);
    bench.Step();

    live = bench.world.World().Digest();
    // The session must actually have DONE something, or the comparison below
    // would pass on two farms where nothing happened.
    CHECK(bench.TheLand().owned == 5);
    CHECK(bench.Barn().Of(content::kFlour) == 1);
  }

  Bench replayed;
  replayed.TheLand().owned = 4;
  replayed.ThePurse().coin = 500;
  for (const LatchedInput& input : recorder.Steps()) {
    replayed.world.Step(input, kDt);
  }
  CHECK(replayed.world.World().Digest() == live);
}

// ---- from the POINTER to the verb -------------------------------------------
//
// The captures show the screens render; this shows that pressing one does
// something. ui::Context takes the pointer as a PARAMETER and a button is
// hit-testing plus arithmetic, so the whole path is reachable with no device: a
// null font skips text and an invalid white handle emits quads nobody looks at.
#include "aether/ui/context.hpp"
#include "screens.hpp"

namespace {

// One frame of UI with the pointer held down at `at`, in design units.
void PressAt(ui::Context& ui, hearthfield::app::Screen& screen,
             hearthfield::app::ScreenCtx& ctx, Vec2 at) {
  constexpr Size kFb{.width = 1280, .height = 720};
  // TWO frames: ui::Context is immediate-mode and a Button fires on RELEASE
  // over a widget it saw pressed, so a single frame of "down" clicks nothing.
  ui.BeginFrame(at, /*pressed=*/true, kFb, nullptr, TextureHandle{}, 720.0f);
  screen.BuildUi(ctx, ui);
  (void)ui.EndFrame();
  ui.BeginFrame(at, /*pressed=*/false, kFb, nullptr, TextureHandle{}, 720.0f);
  screen.BuildUi(ctx, ui);
  (void)ui.EndFrame();
}

}  // namespace

TEST_CASE("PRESSING THE SHOP'S LAND TILE REACHES THE LATCH") {
  Bench bench;
  bench.TheLand().owned = 4;
  bench.ThePurse().coin = 500;
  bench.Step();  // one step so the snapshot is populated

  ui::Context ui;
  hearthfield::app::ScreenCtx ctx{.api = bench.Api(),
                                  .snapshot = &bench.world.Snapshot()};
  hearthfield::app::ShopScreen shop;

  // The first tile sits at the top-left of the panel's body. The panel is
  // centred and 560x420 in design units, so the body starts 16 in from its
  // left edge and 56 down from its top.
  constexpr Vec2 kPanelTopLeft{(1280.0f - 560.0f) * 0.5f,
                               (720.0f - 420.0f) * 0.5f};
  const Vec2 land_tile{kPanelTopLeft.x + 16.0f + 60.0f,
                       kPanelTopLeft.y + 56.0f + 16.0f + 20.0f};

  CHECK_FALSE(bench.pending.buy_land);
  PressAt(ui, shop, ctx, land_tile);
  CHECK(bench.pending.buy_land);

  SUBCASE("and the sim then acts on it, once") {
    bench.Step();
    CHECK(bench.TheLand().owned == 5);
    CHECK_FALSE(bench.pending.buy_land);  // the one-shot was cleared
  }
}

TEST_CASE("pressing empty panel space reaches nothing") {
  // The other half: a press that misses every widget must not fire one. Without
  // this the case above would pass on a screen that set the flag
  // unconditionally.
  Bench bench;
  bench.TheLand().owned = 4;
  bench.ThePurse().coin = 500;
  bench.Step();

  ui::Context ui;
  hearthfield::app::ScreenCtx ctx{.api = bench.Api(),
                                  .snapshot = &bench.world.Snapshot()};
  hearthfield::app::ShopScreen shop;
  PressAt(ui, shop, ctx, Vec2{20.0f, 700.0f});  // outside the panel entirely
  CHECK_FALSE(bench.pending.buy_land);
  CHECK_FALSE(bench.pending.buy_barn);
}
