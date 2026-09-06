// The board camera: gestures, intent arbitration, and motion.
//
// All three units are pure by construction, so the whole FEEL is checkable with
// no window — which matters more here than usual, because feel is the thing
// this camera was asked for and the thing a capture digest cannot see.
#include "hf/content/camera.hpp"

#include <doctest/doctest.h>

#include <bit>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "aether/input/input_recording.hpp"
#include "aether/input/input_snapshot.hpp"
#include "aether/platform/key.hpp"
#include "aether/platform/touch.hpp"
#include "aether/scene_core/camera_component.hpp"
#include "aether/scene_core/orbit_component.hpp"
#include "aether/ui/context.hpp"
#include "hf/content/islands.hpp"
#include "hf/view/camera_gestures.hpp"
#include "hf/view/camera_input.hpp"
#include "hf/view/camera_rig.hpp"

using namespace aether;
using namespace hearthfield;

namespace {

constexpr F32 kDt = 1.0f / 60.0f;
constexpr Vec2 kViewport{1280.0f, 720.0f};
constexpr F32 kViewportHeight = kViewport.y;
// 35 degrees, matching persistent.world.json's authored tilt — the camera moved
// there at sky world s3, because it is the player's and travel unloads the
// farm.
constexpr F32 kPitch = 0.6108652f;

const ui::Context& OpenUi() {
  static ui::Context ui;
  return ui;
}

platform::Touch Finger(
    U32 id, F32 x, F32 y,
    platform::TouchPhase phase = platform::TouchPhase::kMoved) {
  return platform::Touch{.id = id, .position = Vec2{x, y}, .phase = phase};
}

view::CameraRig MakeRig(content::CameraTuning tuning = {}) {
  view::CameraRig rig;
  rig.Configure(tuning, content::BoundsForGrid(8, 1.5f), 0.0f, 12.0f, kPitch);
  return rig;
}

// A rig with the walls pushed far out, for the tests that are about the FILTER
// rather than about the bounds. Without it a 60px drag hits the roam wall in
// five frames and every later sample measures the clamp instead.
view::CameraRig MakeWideRig(content::CameraTuning tuning = {}) {
  view::CameraRig rig;
  const content::CameraBounds wide{.min_x = -10000.0f,
                                   .max_x = 10000.0f,
                                   .min_z = -10000.0f,
                                   .max_z = 10000.0f,
                                   .field_half_extent = std::nullopt};
  rig.Configure(tuning, wide, 0.0f, 12.0f, kPitch);
  return rig;
}

// Settle a rig by running it with no input until the smoothers converge.
void Settle(view::CameraRig& rig, int frames = 240) {
  for (int i = 0; i < frames; ++i) {
    rig.Apply(view::CameraIntent{}, Vec2{}, false, kViewport, kDt);
  }
}

}  // namespace

// --- gestures ----------------------------------------------------------------

TEST_CASE("one finger is a drag, and the first frame reports no motion") {
  view::TouchGestures gestures;
  const std::vector<platform::Touch> down{
      Finger(7, 100.0f, 100.0f, platform::TouchPhase::kBegan)};
  const view::TouchGesture began = gestures.Update(down);
  CHECK(began.fingers == 1);
  CHECK(began.began);
  // A gesture needs two samples before it has moved. Reporting motion from one
  // is how a tap becomes a flick.
  CHECK(began.drag.x == doctest::Approx(0.0f));
  CHECK(began.drag.y == doctest::Approx(0.0f));

  const std::vector<platform::Touch> moved{Finger(7, 130.0f, 90.0f)};
  const view::TouchGesture drag = gestures.Update(moved);
  CHECK(drag.drag.x == doctest::Approx(30.0f));
  CHECK(drag.drag.y == doctest::Approx(-10.0f));
  CHECK(gestures.TravelPixels() > 30.0f);
}

TEST_CASE("two fingers spreading is a pinch, and turning is a twist") {
  view::TouchGestures gestures;
  (void)gestures.Update(std::vector<platform::Touch>{
      Finger(1, 100.0f, 100.0f, platform::TouchPhase::kBegan),
      Finger(2, 200.0f, 100.0f, platform::TouchPhase::kBegan)});

  SUBCASE("spreading to double the span reports a ratio of two") {
    const view::TouchGesture pinch =
        gestures.Update(std::vector<platform::Touch>{
            Finger(1, 50.0f, 100.0f), Finger(2, 250.0f, 100.0f)});
    CHECK(pinch.fingers == 2);
    CHECK(pinch.pinch == doctest::Approx(2.0f));
    // A RATIO, not a pixel difference: the same spread means the same zoom
    // whether the fingers started an inch apart or a hand apart.
    CHECK(pinch.twist == doctest::Approx(0.0f));
    CHECK(pinch.centroid.x == doctest::Approx(150.0f));
  }

  SUBCASE("rotating the pair a quarter turn reports that angle, signed") {
    const view::TouchGesture twist =
        gestures.Update(std::vector<platform::Touch>{
            Finger(1, 150.0f, 50.0f), Finger(2, 150.0f, 150.0f)});
    CHECK(twist.twist == doctest::Approx(kPi * 0.5f).epsilon(0.01));
    CHECK(twist.pinch == doctest::Approx(1.0f));
  }
}

TEST_CASE("A FINGER LIFTING MID-PINCH MUST NOT SPIKE") {
  // THE BUG THIS EXISTS TO PREVENT. Contacts are matched by id, never by index
  // in the span — a finger lifting renumbers the span, so an index match reads
  // the survivor as having teleported to where the other one was, and reports
  // a pinch of several hundred percent in one frame.
  view::TouchGestures gestures;
  (void)gestures.Update(std::vector<platform::Touch>{
      Finger(1, 100.0f, 100.0f, platform::TouchPhase::kBegan),
      Finger(2, 500.0f, 100.0f, platform::TouchPhase::kBegan)});
  (void)gestures.Update(std::vector<platform::Touch>{
      Finger(1, 100.0f, 100.0f), Finger(2, 500.0f, 100.0f)});

  // Finger 1 lifts; only finger 2 remains, now first in the span.
  const view::TouchGesture after =
      gestures.Update(std::vector<platform::Touch>{Finger(2, 500.0f, 100.0f)});
  CHECK(after.fingers == 1);
  CHECK(after.pinch == doctest::Approx(1.0f));
  CHECK(after.drag.x == doctest::Approx(0.0f));
  CHECK(after.drag.y == doctest::Approx(0.0f));
}

TEST_CASE("ONE FINGER SWAPPED FOR ANOTHER IS NOT A PINCH") {
  // THE CASE THAT ACTUALLY NEEDS THE ID MATCH, and it was missing until
  // mutation testing went looking (2026-08-24). Replacing the id compare with
  // `j == i` left every other touch case green, including the lifting one
  // above — because `comparable` also demands `live_count == count_`, so a
  // CHANGE IN FINGER COUNT already suppresses motion on its own.
  //
  // Ids therefore only earn their keep when the count is unchanged and the
  // fingers are not: one lifts as another lands, which is what re-gripping
  // looks like on a real panel. Matching by span position then measures the
  // span between two contacts that were never a pair.
  view::TouchGestures gestures;
  (void)gestures.Update(std::vector<platform::Touch>{
      Finger(1, 100.0f, 100.0f, platform::TouchPhase::kBegan),
      Finger(2, 500.0f, 100.0f, platform::TouchPhase::kBegan)});
  (void)gestures.Update(std::vector<platform::Touch>{
      Finger(1, 100.0f, 100.0f), Finger(2, 500.0f, 100.0f)});

  // Finger 1 lifts and finger 3 lands, in ONE frame: still two contacts, but
  // only one of them was here before. An index match would compare a 10 px span
  // against the previous 400 px one and report a 97% collapse.
  const view::TouchGesture swapped =
      gestures.Update(std::vector<platform::Touch>{
          Finger(2, 500.0f, 100.0f),
          Finger(3, 510.0f, 100.0f, platform::TouchPhase::kBegan)});
  CHECK(swapped.fingers == 2);
  CHECK(swapped.pinch == doctest::Approx(1.0f));
  CHECK(swapped.twist == doctest::Approx(0.0f));
  CHECK(swapped.drag.x == doctest::Approx(0.0f));
  CHECK(swapped.drag.y == doctest::Approx(0.0f));
}

TEST_CASE("every contact lifting ends the gesture exactly once") {
  view::TouchGestures gestures;
  (void)gestures.Update(std::vector<platform::Touch>{
      Finger(3, 10.0f, 10.0f, platform::TouchPhase::kBegan)});
  const view::TouchGesture ended = gestures.Update(std::vector<platform::Touch>{
      Finger(3, 10.0f, 10.0f, platform::TouchPhase::kEnded)});
  CHECK(ended.ended);
  CHECK(ended.fingers == 0);
  // A later empty frame is nothing happening, not another release.
  CHECK_FALSE(gestures.Update(std::vector<platform::Touch>{}).ended);
}

// --- intent: drag versus tap -------------------------------------------------

TEST_CASE("A DRAG TO PAN MUST NOT ALSO SELECT") {
  content::CameraTuning tuning;
  view::CameraInput camera{tuning};
  input::InputSnapshot in;
  in.SetCursor(Vec2{400.0f, 300.0f});
  in.NewFrame();
  in.SetMouseButton(platform::MouseButton::kLeft, true);
  CHECK_FALSE(camera.Poll(in, OpenUi()).tap);

  // Well past the slop.
  in.NewFrame();
  in.SetCursor(Vec2{460.0f, 300.0f});
  (void)camera.Poll(in, OpenUi());

  SUBCASE("releasing after a drag taps nothing") {
    in.NewFrame();
    in.SetMouseButton(platform::MouseButton::kLeft, false);
    CHECK_FALSE(camera.Poll(in, OpenUi()).tap);
  }

  SUBCASE("and coming back to where it started does not resurrect the tap") {
    // Once a drag, always a drag: a circular drag that ends where it began
    // would otherwise select whatever is under it.
    in.NewFrame();
    in.SetCursor(Vec2{400.0f, 300.0f});
    (void)camera.Poll(in, OpenUi());
    in.NewFrame();
    in.SetMouseButton(platform::MouseButton::kLeft, false);
    CHECK_FALSE(camera.Poll(in, OpenUi()).tap);
  }
}

TEST_CASE("a press released inside the slop taps, and does so on RELEASE") {
  content::CameraTuning tuning;
  view::CameraInput camera{tuning};
  input::InputSnapshot in;
  in.SetCursor(Vec2{400.0f, 300.0f});
  in.NewFrame();
  in.SetMouseButton(platform::MouseButton::kLeft, true);
  // Nothing on press: at press time a tap and the first frame of a pan are the
  // same event, which is why selecting on press selects on every drag.
  CHECK_FALSE(camera.Poll(in, OpenUi()).tap);

  in.NewFrame();
  in.SetCursor(Vec2{402.0f, 301.0f});  // a hand is never perfectly still
  CHECK_FALSE(camera.Poll(in, OpenUi()).tap);

  in.NewFrame();
  in.SetMouseButton(platform::MouseButton::kLeft, false);
  CHECK(camera.Poll(in, OpenUi()).tap);
}

TEST_CASE("Q and E ask for one quarter turn each, and the wheel for zoom") {
  content::CameraTuning tuning;
  view::CameraInput camera{tuning};
  input::InputSnapshot in;
  in.NewFrame();
  in.SetKey(platform::Key::kE, true);
  CHECK(camera.Poll(in, OpenUi()).rotate_steps == 1);

  in.NewFrame();
  in.SetKey(platform::Key::kE, false);
  in.SetKey(platform::Key::kQ, true);
  CHECK(camera.Poll(in, OpenUi()).rotate_steps == -1);

  in.NewFrame();
  in.SetKey(platform::Key::kQ, false);
  in.SetScroll(2.0f);
  const view::CameraIntent zoom = camera.Poll(in, OpenUi());
  CHECK(zoom.zoom_notches == doctest::Approx(2.0f));
  CHECK(zoom.has_zoom_anchor);
}

TEST_CASE("the middle button pans and the left one does not") {
  content::CameraTuning tuning;
  view::CameraInput camera{tuning};
  input::InputSnapshot in;
  in.SetCursor(Vec2{100.0f, 100.0f});
  in.NewFrame();
  in.SetMouseButton(platform::MouseButton::kMiddle, true);
  (void)camera.Poll(in, OpenUi());
  in.NewFrame();
  in.SetCursor(Vec2{140.0f, 100.0f});
  const view::CameraIntent pan = camera.Poll(in, OpenUi());
  CHECK(pan.pan_pixels.x == doctest::Approx(40.0f));
  CHECK(pan.direct);
  CHECK(camera.Panning());
}

TEST_CASE("LEFT-DRAG PANS, because on desktop that is what a touchscreen is") {
  // Only the ANDROID backend fills InputSnapshot::Touches(); glfw never does.
  // So a Windows touchscreen reaches the game as synthesized mouse-LEFT events
  // and nothing else — if left-drag does not pan, touch does not pan.
  content::CameraTuning tuning;
  view::CameraInput camera{tuning};
  input::InputSnapshot in;
  in.SetCursor(Vec2{400.0f, 300.0f});
  in.NewFrame();
  in.SetMouseButton(platform::MouseButton::kLeft, true);
  (void)camera.Poll(in, OpenUi());

  // Inside the slop: still a candidate tap, so the board must NOT move yet.
  in.NewFrame();
  in.SetCursor(Vec2{403.0f, 300.0f});
  const view::CameraIntent twitch = camera.Poll(in, OpenUi());
  CHECK(twitch.pan_pixels.x == doctest::Approx(0.0f));
  CHECK_FALSE(camera.Panning());

  // Past it: now it pans, and keeps panning.
  in.NewFrame();
  in.SetCursor(Vec2{450.0f, 300.0f});
  (void)camera.Poll(in, OpenUi());
  in.NewFrame();
  in.SetCursor(Vec2{470.0f, 300.0f});
  const view::CameraIntent pan = camera.Poll(in, OpenUi());
  CHECK(pan.pan_pixels.x == doctest::Approx(20.0f));
  CHECK(pan.direct);
  CHECK(camera.Panning());

  SUBCASE("and releasing hands the coast to inertia, without tapping") {
    in.NewFrame();
    in.SetMouseButton(platform::MouseButton::kLeft, false);
    const view::CameraIntent up = camera.Poll(in, OpenUi());
    CHECK(up.released);
    CHECK_FALSE(up.tap);
    CHECK_FALSE(camera.Panning());
  }
}

TEST_CASE("A REAL FINGER MUST NOT PAN THE BOARD TWICE") {
  // The abstract pointer tracks the primary CONTACT as well as mouse-left, so
  // on a device that reports real touches both paths see the same finger. Left
  // unguarded the board moves at double speed on Android and nowhere else,
  // which is the worst kind of platform bug to go looking for.
  content::CameraTuning tuning;
  view::CameraInput camera{tuning};
  input::InputSnapshot in;
  const std::vector<platform::Touch> down{
      Finger(1, 100.0f, 100.0f, platform::TouchPhase::kBegan)};
  in.NewFrame();
  in.SetTouches(down);
  (void)camera.Poll(in, OpenUi());

  const std::vector<platform::Touch> moved{Finger(1, 160.0f, 100.0f)};
  in.NewFrame();
  in.SetTouches(moved);
  const view::CameraIntent pan = camera.Poll(in, OpenUi());
  // Exactly the gesture's own delta — 60, not 120.
  CHECK(pan.pan_pixels.x == doctest::Approx(60.0f));
  CHECK(pan.direct);
}

TEST_CASE("a tap still works on both devices") {
  content::CameraTuning tuning;

  SUBCASE("mouse: press and release without moving") {
    view::CameraInput camera{tuning};
    input::InputSnapshot in;
    in.SetCursor(Vec2{200.0f, 200.0f});
    in.NewFrame();
    in.SetMouseButton(platform::MouseButton::kLeft, true);
    (void)camera.Poll(in, OpenUi());
    in.NewFrame();
    in.SetMouseButton(platform::MouseButton::kLeft, false);
    CHECK(camera.Poll(in, OpenUi()).tap);
  }

  SUBCASE("finger: touch down and lift without moving") {
    view::CameraInput camera{tuning};
    input::InputSnapshot in;
    in.NewFrame();
    in.SetTouches(std::vector<platform::Touch>{
        Finger(4, 200.0f, 200.0f, platform::TouchPhase::kBegan)});
    (void)camera.Poll(in, OpenUi());
    in.NewFrame();
    in.SetTouches(std::vector<platform::Touch>{
        Finger(4, 200.0f, 200.0f, platform::TouchPhase::kEnded)});
    CHECK(camera.Poll(in, OpenUi()).tap);
  }
}

// --- rig: motion -------------------------------------------------------------

TEST_CASE("a drag tracks the cursor 1:1, through whatever stiffness is set") {
  // At yaw 0 the ground basis is axis-aligned, so the arithmetic is checkable
  // by hand: world-per-pixel is 2 * half_height / viewport_height. Drag right,
  // the world goes right, so the focus goes LEFT.
  const F32 per_pixel = 2.0f * 12.0f / kViewportHeight;
  view::CameraIntent drag;
  drag.direct = true;
  drag.pan_pixels = Vec2{60.0f, 0.0f};

  SUBCASE("stiffness 0 is RIGID — the board is welded to the cursor") {
    content::CameraTuning rigid;
    rigid.drag_smoothing = 0.0f;
    view::CameraRig rig = MakeRig(rigid);
    rig.Apply(drag, Vec2{}, false, kViewport, kDt);
    CHECK(rig.Focus().x == doctest::Approx(-60.0f * per_pixel));
  }

  SUBCASE("with stiffness it LAGS by design, and converges on the same place") {
    // The lag is the point: it filters the jitter in a pointer's per-frame
    // delta. What must not happen is drift — the destination is still exactly
    // the cursor's, so a drag and a stop land where a rigid one would.
    view::CameraRig rig = MakeRig();  // the shipped default
    rig.Apply(drag, Vec2{}, false, kViewport, kDt);
    CHECK(rig.Focus().x > -60.0f * per_pixel);  // has not arrived yet
    CHECK(rig.Focus().x < 0.0f);                // but has set off
    Settle(rig);
    CHECK(rig.Focus().x == doctest::Approx(-60.0f * per_pixel).epsilon(0.001));
  }

  SUBCASE("a held drag does not fall further behind every frame") {
    // A first-order filter reaches a fixed offset against a constant rate and
    // stays there. If the lag GREW, a long drag would leave the board metres
    // behind the finger by the end of it.
    view::CameraRig rig = MakeWideRig();
    F32 lag_early = 0.0f;
    F32 lag_late = 0.0f;
    for (int i = 0; i < 120; ++i) {
      rig.Apply(drag, Vec2{}, false, kViewport, kDt);
      const F32 travelled = -static_cast<F32>(i + 1) * 60.0f * per_pixel;
      if (i == 20) {
        lag_early = rig.Focus().x - travelled;
      }
      if (i == 119) {
        lag_late = rig.Focus().x - travelled;
      }
    }
    CHECK(lag_late == doctest::Approx(lag_early).epsilon(0.02));
  }
}

TEST_CASE("key panning is FRAME-RATE INDEPENDENT") {
  view::CameraRig at30 = MakeRig();
  view::CameraRig at60 = MakeRig();
  view::CameraIntent keys;
  keys.pan_pixels = Vec2{1.0f, 0.0f};  // one axis, held
  for (int i = 0; i < 60; ++i) {
    at30.Apply(keys, Vec2{}, false, kViewport, 1.0f / 30.0f);
    at60.Apply(keys, Vec2{}, false, kViewport, 1.0f / 60.0f);
    at60.Apply(keys, Vec2{}, false, kViewport, 1.0f / 60.0f);
  }
  CHECK(at30.Focus().x == doctest::Approx(at60.Focus().x).epsilon(0.02));
}

TEST_CASE("THE FOCUS NEVER LEAVES THE BOUNDS, however hard it is pushed") {
  view::CameraRig rig = MakeRig();
  const content::CameraBounds bounds = content::BoundsForGrid(8, 1.5f);
  view::CameraIntent shove;
  shove.direct = true;
  shove.pan_pixels = Vec2{-4000.0f, -4000.0f};
  for (int i = 0; i < 120; ++i) {
    rig.Apply(shove, Vec2{}, false, kViewport, kDt);
  }
  CHECK(rig.Focus().x <= doctest::Approx(bounds.max_x));
  CHECK(rig.Focus().y <= doctest::Approx(bounds.max_z));

  SUBCASE("and releasing at the wall does not spring back") {
    // The DESIRED focus is clamped too, not just the current one — otherwise
    // the smoother keeps pulling against a target outside the world and the
    // board drifts on after the finger stops.
    const Vec2 at_wall = rig.Focus();
    view::CameraIntent release;
    release.released = true;
    rig.Apply(release, Vec2{}, false, kViewport, kDt);
    Settle(rig);
    CHECK(rig.Focus().x == doctest::Approx(at_wall.x));
    CHECK(rig.Focus().y == doctest::Approx(at_wall.y));
  }
}

TEST_CASE("zoom clamps at both ends and eases rather than snapping") {
  content::CameraTuning tuning;
  view::CameraRig rig = MakeRig(tuning);
  view::CameraIntent zoom_in;
  zoom_in.zoom_notches = 1.0f;
  rig.Apply(zoom_in, Vec2{}, false, kViewport, kDt);
  // One frame of a 0.10s ease at 1/60 covers about 15% of the way, so the zoom
  // has MOVED but nowhere near arrived — which is the whole point.
  CHECK(rig.HalfHeight() < 12.0f);
  CHECK(rig.HalfHeight() > 12.0f / tuning.zoom_per_notch);

  SUBCASE("all the way in stops at the minimum") {
    for (int i = 0; i < 400; ++i) {
      rig.Apply(zoom_in, Vec2{}, false, kViewport, kDt);
    }
    CHECK(rig.HalfHeight() == doctest::Approx(tuning.min_half_height));
  }

  SUBCASE("all the way out stops at the maximum") {
    view::CameraIntent zoom_out;
    zoom_out.zoom_notches = -1.0f;
    for (int i = 0; i < 400; ++i) {
      rig.Apply(zoom_out, Vec2{}, false, kViewport, kDt);
    }
    CHECK(rig.HalfHeight() == doctest::Approx(tuning.max_half_height));
  }
}

TEST_CASE("THE POINT UNDER THE CURSOR STAYS PUT ACROSS THE WHOLE ZOOM EASE") {
  // The city-builder behaviour, and the reason the anchor is stored as a RATIO
  // rather than applied once: the zoom is smoothed over many frames, so a
  // one-shot correction anchors the first frame and drifts through the rest.
  view::CameraRig rig = MakeRig();
  const Vec2 anchor{5.0f, 3.0f};
  view::CameraIntent zoom;
  zoom.zoom_notches = 1.0f;
  zoom.has_zoom_anchor = true;

  rig.Apply(zoom, anchor, true, kViewport, kDt);
  // The anchor's offset from the focus, measured in half-heights, is what must
  // hold constant — that ratio IS "the same screen position".
  const Vec2 first{(anchor.x - rig.Focus().x) / rig.HalfHeight(),
                   (anchor.y - rig.Focus().y) / rig.HalfHeight()};
  for (int i = 0; i < 30; ++i) {
    rig.Apply(view::CameraIntent{}, anchor, false, kViewport, kDt);
    const Vec2 now{(anchor.x - rig.Focus().x) / rig.HalfHeight(),
                   (anchor.y - rig.Focus().y) / rig.HalfHeight()};
    CHECK(now.x == doctest::Approx(first.x).epsilon(0.02));
    CHECK(now.y == doctest::Approx(first.y).epsilon(0.02));
  }
}

TEST_CASE("rotation reaches the quarter turn and STOPS on it") {
  view::CameraRig rig = MakeRig();
  view::CameraIntent turn;
  turn.rotate_steps = 1;
  rig.Apply(turn, Vec2{}, false, kViewport, kDt);
  // Eased, not cut: one frame is part of the way, never all of it.
  CHECK(rig.Yaw() > 0.0f);
  CHECK(rig.Yaw() < kPi * 0.5f);
  Settle(rig);
  CHECK(rig.Yaw() == doctest::Approx(kPi * 0.5f).epsilon(0.001));

  SUBCASE("and three more turns come back to where it started") {
    for (int i = 0; i < 3; ++i) {
      rig.Apply(turn, Vec2{}, false, kViewport, kDt);
      Settle(rig);
    }
    CHECK(rig.Yaw() == doctest::Approx(2.0f * kPi).epsilon(0.001));
  }
}

TEST_CASE("inertia coasts, decays, and is capped") {
  content::CameraTuning tuning;
  view::CameraRig rig = MakeRig(tuning);
  view::CameraIntent flick;
  flick.direct = true;
  flick.pan_pixels = Vec2{-200.0f, 0.0f};
  rig.Apply(flick, Vec2{}, false, kViewport, kDt);

  view::CameraIntent release;
  release.released = true;
  rig.Apply(release, Vec2{}, false, kViewport, kDt);
  const F32 speed = std::sqrt(rig.Velocity().x * rig.Velocity().x +
                              rig.Velocity().y * rig.Velocity().y);
  // A one-frame flick on a fast screen otherwise produces a velocity that
  // crosses the whole board before the next frame.
  CHECK(speed <= doctest::Approx(tuning.max_inertia_speed));

  const F32 coasted_to = rig.Focus().x;
  Settle(rig);
  CHECK(rig.Focus().x > coasted_to);                 // it kept going
  CHECK(rig.Velocity().x == doctest::Approx(0.0f));  // and then stopped
}

// --- THE VIEW INVARIANT ------------------------------------------------------
//
// "The player must never see a death angle" is not a list of bad cases to
// enumerate — it is ONE property that must hold in every reachable state. So it
// is written once, checked against hand arithmetic so the property itself can
// be trusted, and then asserted over the reachable space two ways: an
// exhaustive sweep of zoom x detent x extreme focus, and a fuzz that drives
// random input for thousands of frames. Enumerating gestures would only ever
// prove the gestures someone thought of.
//
// The camera's pitch is fixed and its yaw is detented, so it can never look
// under the world or roll. The ONLY reachable bad view is the visible ground
// quad running off the edge of the field into the void.

namespace {

// Does everything on screen land on the field?
[[nodiscard]] bool ViewIsOnField(const view::CameraRig& rig, F32 aspect,
                                 F32 field_half) {
  for (const Vec2& corner : view::VisibleGroundQuad(
           rig.Focus(), rig.HalfHeight(), rig.Yaw(), aspect, kPitch)) {
    if (std::abs(corner.x) > field_half + 0.001f ||
        std::abs(corner.y) > field_half + 0.001f) {
      return false;
    }
  }
  return true;
}

// Deterministic, so a failure reproduces exactly. A real RNG in a test buys
// coverage you cannot reproduce, which is the worst of both.
class Lcg {
 public:
  explicit Lcg(U32 seed) : state_(seed) {}
  [[nodiscard]] F32 Next(F32 lo, F32 hi) {
    state_ = state_ * 1664525u + 1013904223u;
    const F32 unit = static_cast<F32>((state_ >> 8) & 0xFFFFFFu) /
                     static_cast<F32>(0xFFFFFFu);
    return lo + unit * (hi - lo);
  }

 private:
  U32 state_;
};

constexpr F32 kAspect = 1280.0f / 720.0f;

// A field DELIBERATELY smaller than the shipped one, so the FIELD clamp is the
// binding constraint. With the real numbers the roam box is always tighter and
// these would pass vacuously — which is how a clamp comes to be "tested" by a
// test that never once exercises it.
[[nodiscard]] content::CameraBounds TightField() {
  return content::CameraBounds{.min_x = -400.0f,
                               .max_x = 400.0f,
                               .min_z = -400.0f,
                               .max_z = 400.0f,
                               .field_half_extent = 60.0f};
}

}  // namespace

TEST_CASE("the visible quad matches hand arithmetic") {
  // The invariant is only worth as much as this: check the property itself
  // before trusting it to police everything else.
  //
  // At yaw 0 the basis is axis-aligned. Half-height 10 at aspect 2 gives 20 of
  // ground across; the 35-degree tilt spreads the same 10 over 10/sin(35) =
  // 17.43 along the view axis. That FORESHORTENING is the term a naive bounds
  // check omits, and omitting it is how a camera that looks bounded still
  // shows the seam.
  const std::array<Vec2, 4> quad =
      view::VisibleGroundQuad(Vec2{}, 10.0f, 0.0f, 2.0f, kPitch);
  F32 max_x = 0.0f;
  F32 max_z = 0.0f;
  for (const Vec2& c : quad) {
    max_x = std::max(max_x, std::abs(c.x));
    max_z = std::max(max_z, std::abs(c.y));
  }
  CHECK(max_x == doctest::Approx(20.0f));
  CHECK(max_z == doctest::Approx(10.0f / std::sin(kPitch)).epsilon(0.001));

  SUBCASE("and a quarter turn swaps the two, as it must") {
    const std::array<Vec2, 4> turned =
        view::VisibleGroundQuad(Vec2{}, 10.0f, kPi * 0.5f, 2.0f, kPitch);
    F32 turned_x = 0.0f;
    for (const Vec2& c : turned) {
      turned_x = std::max(turned_x, std::abs(c.x));
    }
    CHECK(turned_x == doctest::Approx(10.0f / std::sin(kPitch)).epsilon(0.001));
  }
}

TEST_CASE("THE VOID IS UNREACHABLE: an exhaustive sweep of the state space") {
  // Every zoom the player can reach, at every quarter turn, shoved as hard as
  // possible in each of eight directions. If any combination could put the
  // field's edge on screen, this finds it.
  // A SLAB-WORLD tuning, deliberately. The guarantee under test and the zoom
  // ceiling are a MATCHED PAIR: the rig can only keep the view on the field
  // while the view fits on it, and it says so — past that the limit goes to
  // zero and the focus merely pins to centre. Hearthfield's ceiling went 18 ->
  // 45 for the sky world (where running off the rim is the shot), and a 45
  // half-height view reaches ~88 m at a 45 degree detent, which no 60 m field
  // can hold. Pinning the old ceiling here keeps this testing the mechanism
  // rather than testing that an impossible promise is unkept.
  content::CameraTuning tuning;
  tuning.max_half_height = 18.0f;
  const content::CameraBounds tight = TightField();

  for (int detent = 0; detent < 4; ++detent) {
    for (int zoom_steps = -20; zoom_steps <= 20; ++zoom_steps) {
      for (int dir = 0; dir < 8; ++dir) {
        view::CameraRig rig;
        rig.Configure(tuning, tight, 0.0f, 6.0f, kPitch);

        view::CameraIntent set_up;
        set_up.rotate_steps = detent;
        set_up.zoom_notches = static_cast<F32>(zoom_steps);
        rig.Apply(set_up, Vec2{}, false, kViewport, kDt);
        Settle(rig);

        view::CameraIntent shove;
        shove.direct = true;
        const F32 angle = static_cast<F32>(dir) * kPi * 0.25f;
        shove.pan_pixels =
            Vec2{9000.0f * std::cos(angle), 9000.0f * std::sin(angle)};
        for (int i = 0; i < 40; ++i) {
          rig.Apply(shove, Vec2{}, false, kViewport, kDt);
        }
        Settle(rig);

        REQUIRE_MESSAGE(ViewIsOnField(rig, kAspect, *tight.field_half_extent),
                        "detent " << detent << " zoom " << zoom_steps << " dir "
                                  << dir << " focus " << rig.Focus().x << ","
                                  << rig.Focus().y << " half "
                                  << rig.HalfHeight());
      }
    }
  }
}

TEST_CASE("THE VOID IS UNREACHABLE: fuzzing thousands of input frames") {
  // The sweep tests settled states one axis at a time. This tests the
  // COMPOSITION — zooming while panning while turning, mid-ease, with inertia
  // in flight — which is where a clamp that is correct in isolation gets
  // overrun by the correction that runs after it. The zoom anchor is exactly
  // such a correction: it moves the focus AFTER the pan.
  // A SLAB-WORLD tuning, deliberately. The guarantee under test and the zoom
  // ceiling are a MATCHED PAIR: the rig can only keep the view on the field
  // while the view fits on it, and it says so — past that the limit goes to
  // zero and the focus merely pins to centre. Hearthfield's ceiling went 18 ->
  // 45 for the sky world (where running off the rim is the shot), and a 45
  // half-height view reaches ~88 m at a 45 degree detent, which no 60 m field
  // can hold. Pinning the old ceiling here keeps this testing the mechanism
  // rather than testing that an impossible promise is unkept.
  content::CameraTuning tuning;
  tuning.max_half_height = 18.0f;
  const content::CameraBounds tight = TightField();
  view::CameraRig rig;
  rig.Configure(tuning, tight, 0.0f, 6.0f, kPitch);

  Lcg random{12345u};
  for (int frame = 0; frame < 20000; ++frame) {
    view::CameraIntent intent;
    intent.direct = random.Next(0.0f, 1.0f) < 0.6f;
    intent.pan_pixels =
        Vec2{random.Next(-500.0f, 500.0f), random.Next(-500.0f, 500.0f)};
    if (random.Next(0.0f, 1.0f) < 0.2f) {
      intent.zoom_notches = random.Next(-4.0f, 4.0f);
      intent.has_zoom_anchor = true;
    }
    if (random.Next(0.0f, 1.0f) < 0.05f) {
      intent.rotate_steps = random.Next(0.0f, 1.0f) < 0.5f ? -1 : 1;
    }
    intent.released = random.Next(0.0f, 1.0f) < 0.1f;
    // A real ground point, as app/ would unproject it.
    const Vec2 anchor{rig.Focus().x + random.Next(-30.0f, 30.0f),
                      rig.Focus().y + random.Next(-30.0f, 30.0f)};
    // dt VARIES too: a frame spike must not let the board jump the wall.
    rig.Apply(intent, anchor, intent.has_zoom_anchor, kViewport,
              random.Next(1.0f / 240.0f, 1.0f / 20.0f));

    REQUIRE_MESSAGE(ViewIsOnField(rig, kAspect, *tight.field_half_extent),
                    "frame " << frame << " focus " << rig.Focus().x << ","
                             << rig.Focus().y << " half " << rig.HalfHeight());
  }
}

TEST_CASE("a view wider than the field pins to the centre") {
  // Something has to be off-field once the view exceeds the world, so the best
  // available answer is symmetry: centre it, rather than letting the player
  // push the imbalance to one side and stare at the seam.
  const content::CameraTuning tuning;
  const content::CameraBounds small{.min_x = -400.0f,
                                    .max_x = 400.0f,
                                    .min_z = -400.0f,
                                    .max_z = 400.0f,
                                    .field_half_extent = 5.0f};
  view::CameraRig rig;
  rig.Configure(tuning, small, 0.0f, 18.0f, kPitch);
  view::CameraIntent shove;
  shove.direct = true;
  shove.pan_pixels = Vec2{5000.0f, 5000.0f};
  for (int i = 0; i < 60; ++i) {
    rig.Apply(shove, Vec2{}, false, kViewport, kDt);
  }
  CHECK(rig.Focus().x == doctest::Approx(0.0f));
  CHECK(rig.Focus().y == doctest::Approx(0.0f));
}

TEST_CASE("THE SHIPPED CONFIGURATION KEEPS THE FOCUS ON THE ISLAND") {
  // What the game ships changed on 2026-08-26 and so did the invariant. It used
  // to be `the view never leaves the field`, enforced with kFieldHalfExtent
  // mirroring farm.world.json's 120 m slab. The sky world INVERTS that: the
  // view is meant to run off the island's rim into sky, and what must stay
  // bounded is the FOCUS. The old invariant is still tested — TightField()
  // below carries an explicit extent — it is just no longer what Hearthfield
  // uses.
  const content::CameraTuning tuning;
  const content::CameraBounds shipped = content::kIslands[content::kHub].Roam();
  CHECK_FALSE(shipped.field_half_extent.has_value());
  for (int detent = 0; detent < 4; ++detent) {
    view::CameraRig rig;
    rig.Configure(tuning, shipped, 0.785398f, 6.0f, kPitch);
    view::CameraIntent out;
    out.rotate_steps = detent;
    out.zoom_notches = -30.0f;  // all the way out
    rig.Apply(out, Vec2{}, false, kViewport, kDt);
    Settle(rig);
    view::CameraIntent shove;
    shove.direct = true;
    shove.pan_pixels = Vec2{9000.0f, 9000.0f};
    for (int i = 0; i < 40; ++i) {
      rig.Apply(shove, Vec2{}, false, kViewport, kDt);
    }
    Settle(rig);
    // Shoved as hard as the input allows, the focus is still on the island.
    const Vec2 focus = rig.Focus();
    CHECK(focus.x <= shipped.max_x + 1e-3f);
    CHECK(focus.y <= shipped.max_z + 1e-3f);
    CHECK(focus.x >= shipped.min_x - 1e-3f);
    CHECK(focus.y >= shipped.min_z - 1e-3f);
  }
}

TEST_CASE("A FIELD EXTENT, WHEN GIVEN, STILL PINS THE VIEW ON IT") {
  // The mechanism did not go away with Hearthfield's use of it: a slab world
  // still gets the guarantee, and this is what stops the optional turning into
  // dead code nobody notices is never taken.
  // A SLAB-WORLD tuning, deliberately. The guarantee under test and the zoom
  // ceiling are a MATCHED PAIR: the rig can only keep the view on the field
  // while the view fits on it, and it says so — past that the limit goes to
  // zero and the focus merely pins to centre. Hearthfield's ceiling went 18 ->
  // 45 for the sky world (where running off the rim is the shot), and a 45
  // half-height view reaches ~88 m at a 45 degree detent, which no 60 m field
  // can hold. Pinning the old ceiling here keeps this testing the mechanism
  // rather than testing that an impossible promise is unkept.
  content::CameraTuning tuning;
  tuning.max_half_height = 18.0f;
  const content::CameraBounds shipped =
      content::BoundsForGrid(8, 1.5f, 4.0f, 60.0f);
  for (int detent = 0; detent < 4; ++detent) {
    view::CameraRig rig;
    rig.Configure(tuning, shipped, 0.785398f, 6.0f, kPitch);  // authored yaw 45
    view::CameraIntent out;
    out.rotate_steps = detent;
    out.zoom_notches = -30.0f;  // all the way out
    rig.Apply(out, Vec2{}, false, kViewport, kDt);
    Settle(rig);
    view::CameraIntent shove;
    shove.direct = true;
    shove.pan_pixels = Vec2{9000.0f, 9000.0f};
    for (int i = 0; i < 40; ++i) {
      rig.Apply(shove, Vec2{}, false, kViewport, kDt);
    }
    Settle(rig);
    CHECK(ViewIsOnField(rig, kAspect, *shipped.field_half_extent));
  }
}

TEST_CASE("Write hands the scene ONE damping model, not two") {
  view::CameraRig rig = MakeRig();
  scene::OrbitComponent orbit;
  orbit.stiffness = 9.0f;  // whatever the world file or an earlier build set
  scene::CameraComponent projection;
  rig.Write(orbit, projection);
  // The rig owns every ease; leaving the component's on would compose two
  // damping models over the same value.
  CHECK(orbit.stiffness == doctest::Approx(0.0f));
  CHECK(orbit.pivot.y == doctest::Approx(0.0f));  // the focus is on the ground
  CHECK(projection.ortho_half_height == doctest::Approx(rig.HalfHeight()));
}

// ---------------------------------------------------------------------------
// The real-digitiser fixture.
//
// EVERY OTHER TOUCH CASE ABOVE IS SYNTHETIC — spans written by hand in this
// file. That was the standing risk in ADR-0116: only the Android backend fills
// InputSnapshot::Touches(), glfw never does, so nothing had ever fed this
// recognizer contacts that came off real glass. `adb shell input` cannot inject
// a second finger (motionevent needs API 29; the phone is 26) and the emulator
// discards raw /dev/input writes, so the contacts had to be RECORDED on a
// device and carried here.
//
// v2_so02j_pinch.rec is 135 frames from a Sony SO-02J (720x1184, Synaptics
// "clearpad", protocol A), captured through Coin Rush — the 3D path cannot run
// on that phone at all (ADR-0121), and it does not need to: the fixture is raw
// engine-level contacts and TouchGestures is a pure function of a span.
//
// IF platform::Key EVER GAINS AN ENTRY this fixture stops decoding, because the
// format pins the key count. Re-recording needs the phone; re-WIDENING does
// not, and is what to do — every key byte in this recording is zero (the device
// has no keyboard), so the fix is mechanical.
namespace {

// Load one of the committed device recordings. Returns by value; the caller
// REQUIREs, because a missing fixture is a broken checkout, not a failed
// assertion about the camera.
input::InputRecording LoadFixture(const char* name) {
  const std::filesystem::path path =
      std::filesystem::path(AETHER_TEST_ASSET_DIR) / "recordings" / name;
  std::ifstream file(path, std::ios::binary);
  REQUIRE_MESSAGE(file.good(), "missing fixture: " << path.string());
  const std::string raw((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
  std::vector<Byte> bytes(raw.size());
  for (Usize i = 0; i < raw.size(); ++i) {
    bytes[i] = static_cast<Byte>(raw[i]);
  }
  auto recording = input::InputRecording::Decode(bytes, path.string());
  // NOT REQUIRE_MESSAGE: doctest evaluates the message eagerly, so naming
  // .error() there reads the error of a HEALTHY expected on the passing path.
  if (!recording.has_value()) {
    FAIL("fixture rejected: " << recording.error().message);
  }
  return std::move(*recording);
}

// What the recognizer made of a whole recording.
struct Replayed {
  Usize two_finger_gestures = 0;
  F32 widest = 1.0f;    // peak of the COMPOSED pinch ratio
  F32 sharpest = 1.0f;  // worst single-frame ratio — the spike detector
  F32 twist = 0.0f;     // net radians
  F32 twist_cw = 0.0f;  // negative-going only, so fragments cannot cancel
};

Replayed Replay(const input::InputRecording& recording) {
  view::TouchGestures gestures;
  Replayed out;
  F32 product = 1.0f;
  for (const input::RecordedFrame& frame : recording.Frames()) {
    const view::TouchGesture g = gestures.Update(frame.touches);
    if (g.fingers < 2) {
      continue;
    }
    ++out.two_finger_gestures;
    product *= g.pinch;
    out.widest = std::max(out.widest, product);
    out.sharpest = std::max(out.sharpest, g.pinch);
    out.twist += g.twist;
    if (g.twist < 0.0f) {
      out.twist_cw += g.twist;
    }
  }
  return out;
}

}  // namespace

TEST_CASE("REAL CONTACTS from a digitiser drive a real pinch") {
  const input::InputRecording recording = LoadFixture("v2_so02j_pinch.rec");
  REQUIRE(recording.FrameCount() == 135);

  // What only real hardware can establish: two contacts LIVE IN THE SAME FRAME.
  Usize two_finger_frames = 0;
  U32 distinct_ids = 0;
  for (const input::RecordedFrame& frame : recording.Frames()) {
    if (frame.touches.size() >= 2) {
      ++two_finger_frames;
      CHECK(frame.touches[0].id != frame.touches[1].id);
      distinct_ids |= 1u << (frame.touches[0].id & 31u);
      distinct_ids |= 1u << (frame.touches[1].id & 31u);
    }
  }
  CHECK(two_finger_frames == 37);
  // Two different fingers, by id — not one contact reported twice.
  CHECK(std::popcount(distinct_ids) == 2);
  //
  // WHAT THIS FIXTURE CANNOT WITNESS, stated so nobody assumes otherwise: this
  // panel never reorders its span — every one of its frames lists contacts
  // ascending by id, so index-matching and id-matching agree throughout, and a
  // recognizer that matched by span position passes this case. The guard for
  // that is the synthetic "ONE FINGER SWAPPED FOR ANOTHER" above.

  SUBCASE("and TouchGestures turns them into a pinch, not a spike") {
    const Replayed r = Replay(recording);
    // PINNED, not bounded — the numbers are the oracle.
    //
    // Loose bounds were tried first and were worthless: with `> 20` and
    // `> 1.5f` this case survived BOTH mutants aimed at it (matching contacts
    // by span index instead of id, and treating only kMoved as live). A fixture
    // is fixed data through pure float maths, so its outputs are as repeatable
    // as a capture digest, and anything that moves them is a behaviour change
    // worth looking at deliberately.
    //
    // Approx rather than == because atan2/hypot may differ in the last bits
    // between libms; the tolerance is far tighter than any behaviour change
    // and far looser than that.
    CHECK(r.two_finger_gestures == 34);
    // The composed peak. ~12x is several separate spreads multiplying up, not
    // one enormous one — the strongest SINGLE spread here is x3.27.
    CHECK(r.widest == doctest::Approx(12.166f).epsilon(0.001));
    // NO SINGLE-FRAME SPIKE: the worst frame-to-frame ratio is 1.17. A contact
    // teleporting between fingers would put this near 2 or beyond.
    CHECK(r.sharpest == doctest::Approx(1.1664f).epsilon(0.001));

    // Twist is NOT asserted as motion, deliberately: this gesture was a spread,
    // and -0.17 rad (about 10 degrees) over 34 frames is finger jitter.
    // Asserting a rotation the human did not perform is how a test starts
    // passing for the wrong reason — and the first analysis of this recording
    // DID claim +514 degrees of twist, an artefact of ordering contacts by span
    // rather than by id.
    CHECK(r.twist == doctest::Approx(-0.1726f).epsilon(0.01));
  }
}

TEST_CASE("REAL CONTACTS from a digitiser drive a real twist") {
  // The other half, and it needed a second gesture from a human: the pinch
  // fixture contains no rotation at all, so shipping only that one would have
  // left `twist -> rotate` exactly as unverified as before.
  //
  // WHAT THIS PANEL DOES TO A ROTATION, which is the finding worth keeping: the
  // SO-02J reports BOTH contacts in only a minority of its report frames during
  // a two-finger gesture (12 of 76 in a raw `getevent` capture). A sustained
  // turn therefore arrives as a string of short episodes rather than one arc,
  // and `comparable` refuses to measure across the breaks — by design, since
  // the pair either side of a dropout is not the pair that produced the last
  // sample. The rotation is real and one-directional; the recognizer sees it in
  // pieces and loses the motion that happened while a finger was missing.
  const input::InputRecording recording = LoadFixture("v2_so02j_twist.rec");
  REQUIRE(recording.FrameCount() == 235);

  const Replayed r = Replay(recording);
  CHECK(r.two_finger_gestures == 71);

  // A REAL ROTATION: about -63 degrees net, an order of magnitude past the
  // pinch fixture's -10 degrees of jitter, and it survives as net rather than
  // cancelling out — the fingers turned one way.
  CHECK(r.twist == doctest::Approx(-1.1002f).epsilon(0.01));
  CHECK(r.twist < -1.0f);
  // Almost all of it is in the one direction: the clockwise-only sum accounts
  // for nearly the whole net, so this is a turn and not two-way wobble.
  CHECK(r.twist_cw == doctest::Approx(-1.1736f).epsilon(0.01));

  // The fingers also spread while turning — a human hand does both — so this
  // fixture is NOT a clean rotation-only signal, and is not asserted as one.
  CHECK(r.sharpest == doctest::Approx(1.3372f).epsilon(0.001));
}

// --- travel (sky world s5) ---------------------------------------------------

TEST_CASE("A CROSSING CANNOT LEAVE THE ISLAND UNLESS THE BOUNDS LET IT") {
  // THE BUG THIS EXISTS TO PREVENT, and it was a real one: the first version of
  // travel moved the focus toward the target island every frame and the camera
  // never left the hub, because ClampFocus runs inside every Apply and the
  // DEPARTED island's roam rect was still in force. A flight is not exempt from
  // the clamp — it has to be given a rect that contains where it is going.
  const content::CameraTuning tuning;
  const content::Island& from = content::kIslands[content::kHub];
  const content::Island& to = content::kIslands[content::kNearIsle];
  const Vec2 target{to.world_pos.x, to.world_pos.z};

  SUBCASE("with only the departed island's rect, the focus is dragged back") {
    view::CameraRig rig;
    rig.Configure(tuning, from.Roam(), 0.785398f, 20.0f, kPitch);
    rig.SnapFocus(target);
    rig.Apply(view::CameraIntent{}, Vec2{}, false, kViewport, kDt);
    // Short of the target by more than a rounding error: the clamp won.
    CHECK(rig.Focus().x < from.Roam().max_x + 1e-3f);
    CHECK(rig.Focus().x < target.x - 1.0f);
  }

  SUBCASE("with the union of both, the focus arrives and stays") {
    view::CameraRig rig;
    rig.Configure(tuning, from.Roam(), 0.785398f, 20.0f, kPitch);
    const content::CameraBounds a = from.Roam();
    const content::CameraBounds b = to.Roam();
    rig.SetBounds(content::CameraBounds{.min_x = std::min(a.min_x, b.min_x),
                                        .max_x = std::max(a.max_x, b.max_x),
                                        .min_z = std::min(a.min_z, b.min_z),
                                        .max_z = std::max(a.max_z, b.max_z)});
    rig.SnapFocus(target);
    rig.Apply(view::CameraIntent{}, Vec2{}, false, kViewport, kDt);
    CHECK(rig.Focus().x == doctest::Approx(target.x).epsilon(0.01));
    CHECK(rig.Focus().y == doctest::Approx(target.y).epsilon(0.01));
  }
}

TEST_CASE("SNAPPING THE FOCUS KILLS THE INERTIA THAT WOULD DRAG IT BACK") {
  // A flick left running across a crossing would pull the camera back toward
  // the island it just left the moment the player touched the screen. SnapFocus
  // clears the velocity for that reason, and it is invisible without this.
  view::CameraRig rig = MakeWideRig();
  view::CameraIntent flick;
  flick.direct = true;
  flick.pan_pixels = Vec2{400.0f, 0.0f};
  for (int i = 0; i < 5; ++i) {
    rig.Apply(flick, Vec2{}, false, kViewport, kDt);
  }
  REQUIRE(std::abs(rig.Velocity().x) > 0.0f);
  rig.SnapFocus(Vec2{50.0f, 50.0f});
  CHECK(rig.Velocity().x == doctest::Approx(0.0f));
  CHECK(rig.Velocity().y == doctest::Approx(0.0f));
}

TEST_CASE("A FRESH RIG STARTS IN THE MIDDLE OF ITS BOUNDS, NOT AT THE ORIGIN") {
  // This clamped a default (0, 0) until 2026-08-29, which is correct exactly
  // once: the hub's roam box straddles the origin, so the hub framed perfectly
  // and every satellite framed its NEAR EDGE with the farm in the corner.
  // Travel hid it — the flight moves the focus before arrival — but Configure
  // also runs for a save left on a satellite.
  content::CameraTuning tuning;
  const content::CameraBounds offset{
      .min_x = 60.0f, .max_x = 120.0f, .min_z = -15.0f, .max_z = 45.0f};
  view::CameraRig rig;
  rig.Configure(tuning, offset, 0.0f, 20.0f, 0.6f);
  CHECK(rig.Focus().x == doctest::Approx(90.0f));
  CHECK(rig.Focus().y == doctest::Approx(15.0f));

  // The hub's answer is unchanged, which is what makes this safe to change: a
  // box about the origin still centres on the origin.
  const content::CameraBounds about_origin{
      .min_x = -30.0f, .max_x = 30.0f, .min_z = -30.0f, .max_z = 30.0f};
  rig.Configure(tuning, about_origin, 0.0f, 20.0f, 0.6f);
  CHECK(rig.Focus().x == doctest::Approx(0.0f));
  CHECK(rig.Focus().y == doctest::Approx(0.0f));
}

TEST_CASE(
    "A TALLER WINDOW GETS A TALLER HALF-HEIGHT, A WIDER ONE GETS NOTHING") {
  // ortho_half_height is VERTICAL, so the horizontal follows the aspect and a
  // portrait phone opened on a farm cut off at both sides.
  const F32 authored = 20.0f;
  // 16:9 is the shape the framing was authored against — it must not move.
  CHECK(view::FitHalfHeight(authored, Size{1280, 720}) ==
        doctest::Approx(authored));
  CHECK(view::FitHalfHeight(authored, Size{1920, 1080}) ==
        doctest::Approx(authored));

  // 9:16 wants 16/9 / (9/16) = 3.16x, which Configure then clamps.
  CHECK(view::FitHalfHeight(authored, Size{720, 1280}) ==
        doctest::Approx(authored * (16.0f / 9.0f) / (9.0f / 16.0f)));

  // WIDER THAN THE REFERENCE CHANGES NOTHING. Scaling down would zoom an
  // ultrawide IN to claw back ground it is not short of.
  CHECK(view::FitHalfHeight(authored, Size{2560, 1080}) ==
        doctest::Approx(authored));
  // A degenerate size is the authored value rather than a divide by zero.
  CHECK(view::FitHalfHeight(authored, Size{0, 0}) == doctest::Approx(authored));
}
