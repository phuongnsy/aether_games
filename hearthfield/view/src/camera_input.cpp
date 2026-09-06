#include "hf/view/camera_input.hpp"

#include <algorithm>
#include <cmath>

#include "aether/platform/key.hpp"

namespace hearthfield::view {
namespace {

using namespace aether;

// Held either way, so a left-hander and a WASD player get the same board.
[[nodiscard]] Vec2 KeyPan(const input::InputSnapshot& in) {
  using platform::Key;
  Vec2 pan{};
  if (in.IsDown(Key::kA) || in.IsDown(Key::kLeft)) {
    pan.x -= 1.0f;
  }
  if (in.IsDown(Key::kD) || in.IsDown(Key::kRight)) {
    pan.x += 1.0f;
  }
  if (in.IsDown(Key::kW) || in.IsDown(Key::kUp)) {
    pan.y -= 1.0f;
  }
  if (in.IsDown(Key::kS) || in.IsDown(Key::kDown)) {
    pan.y += 1.0f;
  }
  // Diagonals must not be faster than the axes, or the board accelerates when
  // two keys happen to be held.
  const F32 length = std::sqrt(pan.x * pan.x + pan.y * pan.y);
  if (length > 1.0f) {
    pan.x /= length;
    pan.y /= length;
  }
  return pan;
}

}  // namespace

bool CameraInput::Latch(bool active, platform::MouseButton button,
                        const input::InputSnapshot& input,
                        const ui::Context& ui) {
  if (!input.IsMouseDown(button)) {
    return false;
  }
  return active || (input.MouseJustPressed(button) && !ui.WantsPointer());
}

void CameraInput::PollContacts(const input::InputSnapshot& input, bool ui_wants,
                               CameraIntent& out) {
  // Gestures are updated EVERY poll, contacts or not: the recognizer needs the
  // empty frame to see a finger lift.
  const bool has_contacts = !input.Touches().empty();
  const TouchGesture gesture = gestures_.Update(input.Touches());
  if (gesture.began) {
    touch_panning_ = !ui_wants;
  }
  if (gesture.fingers == 0) {
    if (touch_panning_ && gesture.ended) {
      out.released = true;
    }
    touch_panning_ = false;
  }
  if (touch_panning_ && has_contacts && gesture.fingers > 0) {
    out.pan_pixels = gesture.drag;
    out.direct = true;
    if (gesture.fingers == 2) {
      // A pinch ratio IS a zoom factor; converting to notches keeps one path
      // through the rig for wheel and fingers alike.
      if (gesture.pinch != 1.0f && tuning_.zoom_per_notch > 1.0f) {
        out.zoom_notches =
            std::log(gesture.pinch) / std::log(tuning_.zoom_per_notch);
        out.zoom_anchor = gesture.centroid;
        out.has_zoom_anchor = true;
      }
      twist_accumulated_ += gesture.twist;
      while (std::abs(twist_accumulated_) >= tuning_.twist_per_detent) {
        const F32 sign = twist_accumulated_ > 0.0f ? 1.0f : -1.0f;
        out.rotate_steps += static_cast<int>(sign);
        twist_accumulated_ -= sign * tuning_.twist_per_detent;
      }
    } else {
      twist_accumulated_ = 0.0f;
    }
  }
}

void CameraInput::PollPointer(const input::InputSnapshot& input, bool ui_wants,
                              bool has_contacts, CameraIntent& out) {
  // --- The ABSTRACT POINTER: one path for a mouse button and a finger alike,
  // doing two jobs at once — deciding tap versus drag, and panning once it is
  // a drag. Decided on RELEASE: at press time a tap and the first frame of a
  // pan are the same event, so anything acting on press acts on every drag.
  if (input.PointerJustPressed()) {
    press_live_ = !ui_wants;
    press_dragged_ = false;
    press_at_ = input.PointerPosition();
    pointer_last_ = press_at_;
  }
  if (press_live_ && input.IsPointerDown()) {
    const Vec2 now = input.PointerPosition();
    const F32 dx = now.x - press_at_.x;
    const F32 dy = now.y - press_at_.y;
    // Once a drag, always a drag for the rest of the gesture: coming back
    // inside the slop must not resurrect the tap, or a circular drag that ends
    // where it began selects whatever is under it.
    if (dx * dx + dy * dy >
        tuning_.drag_slop_pixels * tuning_.drag_slop_pixels) {
      press_dragged_ = true;
    }
    // Pan only once it IS a drag, and only when the gesture recognizer is not
    // already doing it. The first few pixels before the slop are deliberately
    // dropped: moving the board on a press that might still be a tap is what
    // makes a board feel twitchy.
    if (press_dragged_ && !has_contacts && !dragging_) {
      out.pan_pixels = Vec2{out.pan_pixels.x + now.x - pointer_last_.x,
                            out.pan_pixels.y + now.y - pointer_last_.y};
      out.direct = true;
      pointer_panning_ = true;
    }
    pointer_last_ = now;
  }
  if (input.PointerJustReleased()) {
    out.tap = press_live_ && !press_dragged_ && !ui_wants;
    if (pointer_panning_) {
      out.released = true;  // hand the built-up velocity to inertia
    }
    press_live_ = false;
    press_dragged_ = false;
    pointer_panning_ = false;
  }
}

CameraIntent CameraInput::Poll(const input::InputSnapshot& input,
                               const ui::Context& ui) {
  CameraIntent out;
  const bool ui_wants = ui.WantsPointer();
  // Real contacts own panning outright when the platform reports them, because
  // the abstract pointer ALSO tracks the primary contact — running both paths
  // would move the board twice for one finger.
  const bool has_contacts = !input.Touches().empty();

  PollContacts(input, ui_wants, out);
  // --- Mouse pan: MIDDLE, so left stays selection and right stays free.
  const bool was_dragging = dragging_;
  dragging_ = Latch(dragging_, platform::MouseButton::kMiddle, input, ui);
  if (dragging_) {
    out.pan_pixels = input.CursorDelta();
    out.direct = true;
  } else if (was_dragging) {
    out.released = true;
  }

  // --- Keys. Smoothed rather than direct, and additive with a drag so holding
  // a key while dragging does not fight.
  if (const Vec2 keys = KeyPan(input); keys.x != 0.0f || keys.y != 0.0f) {
    out.pan_pixels = Vec2{out.pan_pixels.x + keys.x, out.pan_pixels.y + keys.y};
    // Deliberately NOT setting `direct`: the rig reads key pan in half-heights
    // per second, and a drag in pixels. A frame with both is rare and resolves
    // as smoothed, which is the safer of the two to be wrong about.
  }

  // --- Wheel zoom, anchored on the cursor.
  if (const F32 scroll = input.ScrollDelta(); scroll != 0.0f && !ui_wants) {
    out.zoom_notches += std::clamp(scroll, -tuning_.max_notches_per_frame,
                                   tuning_.max_notches_per_frame);
    out.zoom_anchor = input.CursorPosition();
    out.has_zoom_anchor = true;
  }

  // --- Q/E quarter turns.
  if (input.JustPressed(platform::Key::kQ)) {
    --out.rotate_steps;
  }
  if (input.JustPressed(platform::Key::kE)) {
    ++out.rotate_steps;
  }
  PollPointer(input, ui_wants, has_contacts, out);
  return out;
}

}  // namespace hearthfield::view
