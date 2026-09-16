// NeuronClient/Window.h
#pragma once

#include "NeuronCore.h"

#include <cstdint>
#include <string_view>

namespace Neuron
{

// The screen the game presents, fixed (AGENTS.md R12): 1280x720 physical pixels, drawn straight into the swap chain's
// back buffer and presented 1:1. The window's CLIENT area is exactly these pixels on every display scaling, which is
// what the per-monitor-v2 awareness in the executable's manifest and the DPI-aware frame arithmetic below are for.
inline constexpr std::uint32_t SCREEN_WIDTH_PIXELS = 1280;
inline constexpr std::uint32_t SCREEN_HEIGHT_PIXELS = 720;

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
    std::wstring_view title;
  };

  Window() = default;
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  Window(Window&&) = delete;
  Window& operator=(Window&&) = delete;
  ~Window();

  /// Creates the window, hidden, with a client area of exactly the requested pixels. Returns false if the class cannot
  /// be registered, the window cannot be created, or the client area did not come out the size asked for.
  [[nodiscard]] static bool Create(const Desc& _desc, Window& _outWindow) noexcept;

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
};

} // namespace Neuron
