// Picking, headless.
//
// This is the half of spec check 1 that needs no GPU, and it is the half where
// the failures actually live: a mirrored Y, a zoom wired to distance, or a
// pick that resolves to the ground behind a crop all look like "the tap was a
// bit off" on a screenshot and like arithmetic here.
#include <doctest/doctest.h>

#include <cmath>
#include <numbers>
#include <vector>

#include "aether/core/render_frame.hpp"
#include "hf/content/farm.hpp"
#include "hf/runtime/grid.hpp"

using namespace aether;
using hearthfield::runtime::Grid;
using hearthfield::runtime::PickPlot;
using hearthfield::runtime::PlotState;
using hearthfield::runtime::PlotView;
namespace content = hearthfield::content;
namespace runtime = hearthfield::runtime;

namespace {

constexpr Viewport kView{.width = 1280, .height = 720};

// The farm camera as farm.world.json authors it: parallel, six metres of
// half-height, looking down at 35° from a 45° azimuth.
[[nodiscard]] Camera FarmCamera(F32 yaw_degrees = 45.0f) {
  const F32 yaw = yaw_degrees * kDegToRad;
  const F32 pitch = 35.0f * kDegToRad;
  const F32 distance = 40.0f;
  // OrbitComponent's own placement, copied deliberately rather than
  // approximated: position and rotation must come from the SAME yaw/pitch pair
  // or the pivot drifts off centre and every expectation below shifts with it.
  const F32 cos_p = std::cos(pitch);
  Camera camera;
  camera.projection = ProjectionMode::kOrthographic3D;
  camera.ortho_half_height = 6.0f;
  camera.near_z = 0.1f;
  camera.far_z = 200.0f;
  camera.position =
      Vec3{cos_p * std::sin(yaw) * distance, std::sin(pitch) * distance,
           cos_p * std::cos(yaw) * distance};
  camera.rotation = QuatFromAxisAngle(Vec3{0.0f, 1.0f, 0.0f}, yaw) *
                    QuatFromAxisAngle(Vec3{1.0f, 0.0f, 0.0f}, -pitch);
  return camera;
}

[[nodiscard]] std::vector<PlotView> EmptyBoard(const Grid& grid) {
  return std::vector<PlotView>(grid.Count());
}

}  // namespace

TEST_CASE("the grid is centred on the origin and evenly spaced") {
  const Grid grid{.columns = 4, .cell = 1.0f};
  CHECK(grid.Count() == 16);
  // Four columns of 1 m: centres at -1.5, -0.5, +0.5, +1.5.
  CHECK(grid.CenterOf(0).x == doctest::Approx(-1.5f));
  CHECK(grid.CenterOf(0).z == doctest::Approx(-1.5f));
  CHECK(grid.CenterOf(3).x == doctest::Approx(1.5f));
  CHECK(grid.CenterOf(3).z == doctest::Approx(-1.5f));
  CHECK(grid.CenterOf(15).x == doctest::Approx(1.5f));
  CHECK(grid.CenterOf(15).z == doctest::Approx(1.5f));
  // The board straddles the origin, so opposite corners are mirror images.
  CHECK(grid.CenterOf(0).x == doctest::Approx(-grid.CenterOf(15).x));
}

TEST_CASE("A TAP AT THE SCREEN CENTRE HITS THE MIDDLE OF THE BOARD") {
  // The camera looks at the origin, and the board is centred there — so NDC
  // (0,0) must land on a plot adjacent to the centre. With an even column
  // count there is no single middle plot, so the assertion is that the hit is
  // one of the four straddling it.
  const Grid grid{.columns = 4, .cell = 1.0f};
  const auto plots = EmptyBoard(grid);
  const Ray3 ray = ScreenRay(FarmCamera(), Vec2{0.0f, 0.0f}, kView);

  const auto hit = PickPlot(grid, plots, ray);
  REQUIRE(hit.has_value());
  const Vec3 centre = grid.CenterOf(*hit);
  CHECK(std::abs(centre.x) == doctest::Approx(0.5f));
  CHECK(std::abs(centre.z) == doctest::Approx(0.5f));
}

TEST_CASE("A TAP OFF THE BOARD HITS NOTHING") {
  // The board is 4 m across and the view is 12 m tall, so the screen corner is
  // well outside it. A picker that clamped instead of missing would plant a
  // crop every time the player tapped the sky.
  const Grid grid{.columns = 4, .cell = 1.0f};
  const auto plots = EmptyBoard(grid);
  CHECK_FALSE(
      PickPlot(grid, plots, ScreenRay(FarmCamera(), Vec2{-1.0f, -1.0f}, kView))
          .has_value());
  CHECK_FALSE(
      PickPlot(grid, plots, ScreenRay(FarmCamera(), Vec2{1.0f, 1.0f}, kView))
          .has_value());
}

TEST_CASE("+Y IN NDC PICKS FURTHER AWAY, WHICH IS THE Y FLIP") {
  // The sign error this whole conversion is about. Under a camera looking down
  // from +Y, up the screen is away from the viewer — so a tap higher on screen
  // must select a plot with a SMALLER row index (further along -Z after the
  // 45° yaw puts the far corner at plot 0).
  const Grid grid{.columns = 6, .cell = 1.0f};
  const auto plots = EmptyBoard(grid);
  const auto near_hit =
      PickPlot(grid, plots, ScreenRay(FarmCamera(), Vec2{0.0f, -0.15f}, kView));
  const auto far_hit =
      PickPlot(grid, plots, ScreenRay(FarmCamera(), Vec2{0.0f, 0.15f}, kView));
  REQUIRE(near_hit.has_value());
  REQUIRE(far_hit.has_value());
  // Further UP the screen is further from the camera.
  const Vec3 near_at = grid.CenterOf(*near_hit);
  const Vec3 far_at = grid.CenterOf(*far_hit);
  const Camera camera = FarmCamera();
  CHECK(Length(far_at - camera.position) > Length(near_at - camera.position));
}

TEST_CASE("A TALL CROP IS PICKED BEFORE THE GROUND BEHIND IT") {
  // The reason picking uses boxes as tall as the crop instead of intersecting
  // the ground plane. A ripe stalk stands above its plot and, under a tilted
  // camera, covers the plot behind it — ground-plane arithmetic would plant a
  // crop one row back from the one the player actually tapped.
  const Grid grid{.columns = 6, .cell = 1.0f};
  std::vector<PlotView> plots = EmptyBoard(grid);

  // Aim at a point and find what a FLAT board answers.
  const Vec2 ndc{0.0f, 0.05f};
  const Ray3 ray = ScreenRay(FarmCamera(), ndc, kView);
  const auto flat = PickPlot(grid, plots, ray);
  REQUIRE(flat.has_value());

  // Now grow a crop on the plot NEARER the camera along that ray and re-pick:
  // the answer must change to the nearer plot, because the stalk now stands in
  // the way.
  const auto nearer = static_cast<runtime::PlotId>(*flat + grid.columns);
  REQUIRE(grid.Has(nearer));
  plots[nearer] =
      PlotView{.state = PlotState::kReady, .crop = 0, .growth = 1.0f};
  const auto grown = PickPlot(grid, plots, ray);
  REQUIRE(grown.has_value());
  CHECK(*grown == nearer);
  CHECK(*grown != *flat);
}

TEST_CASE("a pick box is as tall as what stands on the plot") {
  const Grid grid{.columns = 2, .cell = 1.0f};
  const PlotView empty{};
  const PlotView ripe{.state = PlotState::kReady, .crop = 0, .growth = 1.0f};
  CHECK(runtime::PlotHeight(empty) == doctest::Approx(content::kTileHeight));
  CHECK(runtime::PlotHeight(ripe) ==
        doctest::Approx(content::kTileHeight + content::kCropHeight));

  // A crop sown one tick ago is not a zero-height sliver: it would read as an
  // empty plot and the tap would seem to have missed.
  const PlotView sprout{
      .state = PlotState::kGrowing, .crop = 0, .growth = 0.0f};
  CHECK(runtime::PlotHeight(sprout) > content::kTileHeight);

  const AABB box = runtime::PickBoxOf(grid, 0, ripe);
  CHECK(box.max.y == doctest::Approx(runtime::PlotHeight(ripe)));
  CHECK(box.max.x - box.min.x == doctest::Approx(grid.cell));
}

TEST_CASE("EVERY 90 DEGREE DETENT STILL PICKS THE PLOT UNDER THE POINTER") {
  // The four azimuths are the whole camera design, and a picking bug that only
  // appears at one of them is exactly what a screenshot from the default angle
  // would miss.
  const Grid grid{.columns = 5, .cell = 1.0f};
  const auto plots = EmptyBoard(grid);
  for (int detent = 0; detent < 4; ++detent) {
    CAPTURE(detent);
    const Camera camera = FarmCamera(45.0f + 90.0f * static_cast<F32>(detent));
    const auto hit =
        PickPlot(grid, plots, ScreenRay(camera, Vec2{0.0f, 0.0f}, kView));
    REQUIRE(hit.has_value());
    // Five columns, so the centre plot is unambiguous at every angle.
    CHECK(*hit == 12);
  }
}

TEST_CASE("ZOOM IS THE HALF-HEIGHT: HALVING IT HALVES WHAT THE SCREEN COVERS") {
  // The silent-failure trap. Under a parallel projection, moving the camera
  // changes only the near/far window — so a zoom wired to `distance` resizes
  // nothing and reports no error. This asserts the working knob and the broken
  // one in the same case.
  const Grid grid{.columns = 20, .cell = 1.0f};
  const auto plots = EmptyBoard(grid);
  const Vec2 edge{0.0f, 0.9f};

  Camera wide = FarmCamera();
  const auto far_plot = PickPlot(grid, plots, ScreenRay(wide, edge, kView));

  Camera tight = wide;
  tight.ortho_half_height = wide.ortho_half_height * 0.5f;
  const auto near_plot = PickPlot(grid, plots, ScreenRay(tight, edge, kView));

  REQUIRE(far_plot.has_value());
  REQUIRE(near_plot.has_value());
  // Zoomed in, the same screen point is nearer the board's centre.
  CHECK(Length(grid.CenterOf(*near_plot)) < Length(grid.CenterOf(*far_plot)));

  SUBCASE("DISTANCE, by contrast, changes nothing about the framing") {
    Camera moved = wide;
    moved.position = wide.position * 0.5f;
    const auto same = PickPlot(grid, plots, ScreenRay(moved, edge, kView));
    REQUIRE(same.has_value());
    CHECK(*same == *far_plot);
  }
}
