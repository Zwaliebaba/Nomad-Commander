// NeuronClient/Window.h
#pragma once

#include "NeuronCore.h"

#include <cstdint>

namespace Neuron
{

// The screen the game presents, fixed (AGENTS.md R12): 1920x1080 physical pixels, drawn straight into the swap
// chain's back buffer and presented 1:1. The window's CLIENT area is exactly these pixels on every display scaling,
// which is what the per-monitor-v2 awareness in the executable's manifest and the DPI-aware frame arithmetic below
// are for.
//
// Note what this size does NOT fit: a 1920x1080 desktop. The frame adds a caption and borders, so the window is taller
// than a 1080p screen and the taskbar takes more again. Nothing here scales to compensate, because R12 forbids it --
// the window simply extends past the edges. Whether the game should instead refuse to start on a desktop that cannot
// hold it is an open question for the owner; the code assumes no answer.
inline constexpr std::uint32_t SCREEN_WIDTH_PIXELS = 1920;
inline constexpr std::uint32_t SCREEN_HEIGHT_PIXELS = 1080;

/// Why a window could not be created (the shape of AGENTS.md's worked example). A creation function that returns a
/// bare false tells a caller nothing it can act on, and tells a build agent's log nothing at all.
enum class WindowFault : std::uint8_t
{
  None,
  ClassRegistration,
  FrameArithmetic,
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

  /// Creates the window, hidden, with a client area of exactly the requested pixels. Returns false if the class cannot
  /// be registered, the window cannot be created, or the client area did not come out the size asked for; Fault() and
  /// SystemError() then say which, and what Windows called it.
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
  /// this is what it got instead of what it asked for, which is the one number a build agent's log needs.
  [[nodiscard]] std::uint32_t MeasuredWidthPixels() const noexcept
  {
    return m_measuredWidthPixels;
  }

  [[nodiscard]] std::uint32_t MeasuredHeightPixels() const noexcept
  {
    return m_measuredHeightPixels;
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
  unsigned long m_systemError = 0;
  std::uint32_t m_measuredWidthPixels = 0;
  std::uint32_t m_measuredHeightPixels = 0;
};

} // namespace Neuron
