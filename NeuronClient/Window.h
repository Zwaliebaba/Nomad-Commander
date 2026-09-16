// NeuronClient/Window.h
#pragma once

#include "NeuronCore.h"

#include <cstdint>

namespace Neuron
{

// The screen the game DRAWS, fixed (AGENTS.md R12, ADR-009): 1920x1080 physical pixels. Every pass draws into a scene
// target of exactly this size and the frame ends by presenting that target into the window's client area, scaled with
// the aspect ratio preserved. So these are not the window's pixels -- they are the only size anything draws at, and a
// pass that asks the window how big it is has misunderstood the rule.
inline constexpr std::uint32_t SCREEN_WIDTH_PIXELS = 1920;
inline constexpr std::uint32_t SCREEN_HEIGHT_PIXELS = 1080;

/// Why a window could not be created (the shape of AGENTS.md's worked example). A creation function that returns a
/// bare false tells a caller nothing it can act on, and tells a build agent's log nothing at all.
enum class WindowFault : std::uint8_t
{
  None,
  ClassRegistration,
  FrameArithmetic,
  DesktopTooSmall,
  Creation,
  ClientAreaMismatch
};

/// The one place this tree calls the Win32 window functions. It owns one top-level window, of a fixed size, that
/// cannot be resized or maximized, and pumps messages without ever blocking the frame.
///
/// A window is created into an out parameter and destroyed with the object; it is neither copied nor moved, because
/// the window procedure holds a pointer to it.
class Window
{
public:
  struct Desc
  {
    std::uint32_t clientWidthPixels;
    std::uint32_t clientHeightPixels;
    // A null-terminated literal, handed to CreateWindowExW as it stands. Not a std::wstring_view: Create is noexcept,
    // and copying a view into a string to null-terminate it is an allocation, which is a throw, which in a noexcept
    // function is std::terminate. R13 makes every title in this game a compile-time literal anyway.
    const wchar_t* title;
  };

  Window() = default;
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  Window(Window&&) = delete;
  Window& operator=(Window&&) = delete;
  ~Window();

  /// Creates the window, hidden, with a client area of exactly the requested pixels **where the desktop can hold one
  /// that big**, and otherwise the largest area of the same shape that the work area can hold (the *fit* policy,
  /// ADR-009). The game still draws at 1920x1080 either way; the frame's last step scales that into whatever came out
  /// here, which is what makes a display smaller than the screen usable at all.
  ///
  /// Returns false if the class cannot be registered, the frame arithmetic fails, the work area cannot hold a window
  /// of any size, the window cannot be created, or the client area did not come out the size this function computed.
  /// Fault() and SystemError() then say which, and what Windows called it.
  [[nodiscard]] static bool Create(const Desc& _desc, Window& _outWindow) noexcept;

  /// Why the last Create on this object failed, and the GetLastError value at that moment. None and zero after a
  /// creation that worked.
  [[nodiscard]] WindowFault Fault() const noexcept
  {
    return m_fault;
  }

  [[nodiscard]] unsigned long SystemError() const noexcept
  {
    return m_systemError;
  }

  /// The client area Windows gave the window when Create checked it, whatever the outcome. On a ClientAreaMismatch
  /// this is what it got instead of what Create computed, which is the one number a build agent's log needs.
  [[nodiscard]] std::uint32_t MeasuredWidthPixels() const noexcept
  {
    return m_measuredWidthPixels;
  }

  [[nodiscard]] std::uint32_t MeasuredHeightPixels() const noexcept
  {
    return m_measuredHeightPixels;
  }

  /// Whether the desktop could not hold the requested client area, so Create shrank it. False means the client area is
  /// exactly what was asked for and the present scale is 1:1; true means the renderer is scaling, and the log says so.
  [[nodiscard]] bool FittedToDesktop() const noexcept
  {
    return m_fittedToDesktop;
  }

  /// Makes the window visible. Separate from Create so that a test can make a window without putting one on a screen.
  void Show() noexcept;

  [[nodiscard]] HWND Handle() const noexcept
  {
    return m_handle;
  }

  /// Handles everything waiting and returns at once; false once the window has been closed. PeekMessage, never
  /// GetMessage: a frame must not wait for a message that may not come.
  [[nodiscard]] bool PumpMessages() noexcept;

  /// The client area as Windows reports it, for a caller that wants to check rather than assume.
  [[nodiscard]] bool ClientSizePixels(std::uint32_t& _outWidth, std::uint32_t& _outHeight) const noexcept;

  /// Asks the window to close, as the close box does. The next pump reports it.
  void RequestClose() noexcept;

  [[nodiscard]] bool Closed() const noexcept
  {
    return m_closed;
  }

private:
  static LRESULT CALLBACK WindowProcedure(HWND _handle, UINT _message, WPARAM _wparam, LPARAM _lparam) noexcept;

  HWND m_handle = nullptr;
  bool m_closed = false;
  WindowFault m_fault = WindowFault::None;
  bool m_fittedToDesktop = false;
  unsigned long m_systemError = 0;
  std::uint32_t m_measuredWidthPixels = 0;
  std::uint32_t m_measuredHeightPixels = 0;
};

} // namespace Neuron
