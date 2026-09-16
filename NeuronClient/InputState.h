// NeuronClient/InputState.h
#pragma once

#include "NeuronCore.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace Neuron
{

/// The mouse buttons the desk uses. There is no fourth: GDD §3 is a pointer and a keyboard.
enum class MouseButton : std::uint8_t
{
  Left,
  Right,
  Middle
};

/// A position in the window's client pixels, with the origin at the top left.
struct MousePoint
{
  std::int32_t xPixels;
  std::int32_t yPixels;
};

/// One frame's worth of mouse and keyboard, with the edges already worked out.
///
/// The UI asks "was this clicked" rather than keeping its own copy of last frame's state, which is what makes an
/// immediate-mode widget set possible (NC-025). A frame looks like: BeginFrame, then pump the window, then draw.
/// BeginFrame rolls the edges; the messages that arrive during the pump set them; the drawing reads them.
///
/// **A press and a release inside one frame are both reported.** The edges are set by the messages rather than
/// derived from comparing two frames, so a click faster than a frame is not swallowed — which is a real event on a
/// 144 Hz display and a genuinely fast mouse.
class InputState
{
public:
  static constexpr std::size_t MOUSE_BUTTON_COUNT = 3;

  /// Virtual-key codes are a byte, so this is all of them.
  static constexpr std::size_t KEY_COUNT = 256;

  /// Characters a frame may carry. The desk has a few numeric fields, not a text editor; anything beyond this is
  /// dropped rather than grown into, and Overflowed() says so.
  static constexpr std::size_t MAX_TYPED_CHARACTERS = 64;

  /// The shape Window wants: a plain function pointer and a context, so that connecting the two allocates nothing and
  /// Window needs to know nothing about input.
  [[nodiscard]] static bool MessageSink(void* _context, UINT _message, WPARAM _wparam, LPARAM _lparam) noexcept;

  /// Rolls the edges and clears the frame's typed characters and wheel movement. Called before the window is pumped.
  void BeginFrame() noexcept;

  /// Takes one window message. True if it was one this cares about, which is the answer Window forwards.
  [[nodiscard]] bool HandleMessage(UINT _message, WPARAM _wparam, LPARAM _lparam) noexcept;

  [[nodiscard]] MousePoint MousePosition() const noexcept
  {
    return m_mouse;
  }

  /// How far the mouse moved this frame. Zero on the frame it first arrives, which is what stops a jump.
  [[nodiscard]] MousePoint MouseDelta() const noexcept
  {
    return m_mouseDelta;
  }

  [[nodiscard]] bool MouseDown(MouseButton _button) const noexcept;
  [[nodiscard]] bool MousePressed(MouseButton _button) const noexcept;
  [[nodiscard]] bool MouseReleased(MouseButton _button) const noexcept;

  /// Wheel movement this frame, in notches: one detent is 1, away from the user is positive.
  [[nodiscard]] std::int32_t WheelDelta() const noexcept
  {
    return m_wheelDelta;
  }

  [[nodiscard]] bool KeyDown(std::uint8_t _virtualKey) const noexcept;
  [[nodiscard]] bool KeyPressed(std::uint8_t _virtualKey) const noexcept;
  [[nodiscard]] bool KeyReleased(std::uint8_t _virtualKey) const noexcept;

  /// What was typed this frame, as UTF-16 code units, which is what WM_CHAR delivers.
  [[nodiscard]] std::u16string_view TypedCharacters() const noexcept;

  /// True when this frame's typing did not fit.
  [[nodiscard]] bool TypingOverflowed() const noexcept
  {
    return m_typingOverflowed;
  }

  /// Whether the window has the keyboard. False clears everything held, so nothing sticks down across an Alt+Tab.
  [[nodiscard]] bool HasFocus() const noexcept
  {
    return m_hasFocus;
  }

private:
  /// Drops everything held and reports it as released, so a drag that was in progress ends rather than hanging. A
  /// widget holding a drag needs the release edge, not just the absence of the button.
  void ReleaseEverything() noexcept;

  std::array<bool, MOUSE_BUTTON_COUNT> m_mouseDown{};
  std::array<bool, MOUSE_BUTTON_COUNT> m_mousePressed{};
  std::array<bool, MOUSE_BUTTON_COUNT> m_mouseReleased{};
  std::array<bool, KEY_COUNT> m_keyDown{};
  std::array<bool, KEY_COUNT> m_keyPressed{};
  std::array<bool, KEY_COUNT> m_keyReleased{};
  std::array<char16_t, MAX_TYPED_CHARACTERS> m_typed{};
  MousePoint m_mouse{0, 0};
  MousePoint m_mouseDelta{0, 0};
  std::int32_t m_wheelDelta = 0;
  std::size_t m_typedCount = 0;
  bool m_typingOverflowed = false;
  bool m_hasFocus = true;
  bool m_haveMousePosition = false;
};

} // namespace Neuron
