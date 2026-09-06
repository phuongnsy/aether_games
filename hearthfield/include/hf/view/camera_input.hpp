// Devices -> one device-independent intent. The only file that knows a mouse
// from a finger — and the reason the rest of the camera does not have to.
//
// BOTH POINTERS WORK EVERYWHERE, and on desktop that is not a courtesy: only
// the ANDROID backend fills `InputSnapshot::Touches()`
// (`platform::InputDevice::TouchesThisFrame` defaults to empty and glfw never
// overrides it), so a Windows touchscreen arrives here as synthesized
// mouse-LEFT events and nothing else. Panning therefore hangs off the ABSTRACT
// POINTER — "mouse-left or the primary contact, whichever is live" — which is
// the one control both devices feed. Middle-drag stays as a second, explicit
// pan for mouse users who expect it and never want to risk a selection.
//
// It also owns the DRAG-VERSUS-TAP decision, and that is not an accident of
// placement: whoever holds the press latch is the only thing that knows whether
// a press became a drag, so splitting the two would put two files in charge of
// what one press meant. `tap` comes out of here already arbitrated.
#pragma once

#include "aether/core/math/vec.hpp"
#include "aether/core/types.hpp"
#include "aether/input/input_snapshot.hpp"
#include "aether/ui/context.hpp"
#include "hf/content/camera.hpp"
#include "hf/view/camera_gestures.hpp"

namespace hearthfield::view {

// What the player asked the camera to do this frame, in units no device
// appears in. Pan is in PIXELS because that is the only frame both a mouse and
// a finger agree on; the rig converts once, knowing the zoom.
struct CameraIntent {
  aether::Vec2 pan_pixels{};
  // A drag is 1:1 with the cursor and must not be smoothed (the world sticks to
  // the finger); keys are smoothed. The rig branches on this.
  bool direct = false;
  // The drag ended this frame — hand the built-up velocity to inertia.
  bool released = false;

  aether::F32 zoom_notches = 0.0f;
  // Where to anchor the zoom, in framebuffer pixels. Only meaningful when
  // `zoom_notches` is non-zero.
  aether::Vec2 zoom_anchor{};
  bool has_zoom_anchor = false;

  // Quarter turns, signed. Never a continuous angle — see content/camera.hpp.
  int rotate_steps = 0;

  // A press that came up without becoming a drag, and did not start on the UI.
  bool tap = false;
};

class CameraInput {
 public:
  explicit CameraInput(const content::CameraTuning& tuning) : tuning_(tuning) {}

  [[nodiscard]] CameraIntent Poll(const aether::input::InputSnapshot& input,
                                  const aether::ui::Context& ui);

  // Whether a camera gesture is in flight. app/ suppresses hover resolution
  // while it is, so a plot does not stay lit under a cursor that is panning.
  [[nodiscard]] bool Panning() const {
    return dragging_ || touch_panning_ || pointer_panning_;
  }

 private:
  // Held-since-a-press-that-started-off-the-UI. The same latch shape
  // `app::OrbitInput` uses: releasing always clears, so a button let go outside
  // the window cannot leave a drag stuck on.
  [[nodiscard]] static bool Latch(bool active,
                                  aether::platform::MouseButton button,
                                  const aether::input::InputSnapshot& input,
                                  const aether::ui::Context& ui);

  // The three stateful halves of Poll, split out because each owns different
  // members and they were only ever adjacent, never entangled: contacts drive
  // `touch_panning_`/`twist_accumulated_`, the abstract pointer drives the
  // `press_*` latches, and the rest of Poll is stateless.
  void PollContacts(const aether::input::InputSnapshot& input, bool ui_wants,
                    CameraIntent& out);
  void PollPointer(const aether::input::InputSnapshot& input, bool ui_wants,
                   bool has_contacts, CameraIntent& out);

  content::CameraTuning tuning_;
  TouchGestures gestures_;

  bool dragging_ = false;         // middle button
  bool touch_panning_ = false;    // real contacts driving the board (Android)
  bool pointer_panning_ = false;  // the abstract pointer, past the slop
  aether::Vec2 pointer_last_{};   // there is no PointerDelta(), so track it

  // The primary-pointer press being tracked for tap-versus-drag.
  bool press_live_ = false;
  bool press_dragged_ = false;
  aether::Vec2 press_at_{};

  // Twist accumulates until it buys a quarter turn, then resets by exactly one
  // detent's worth so a long continuous twist keeps yielding steps.
  aether::F32 twist_accumulated_ = 0.0f;
};

}  // namespace hearthfield::view
