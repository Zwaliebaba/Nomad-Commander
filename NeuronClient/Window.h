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
  MonitorQuery,
  DesktopTooSmall,
  Creation,
  ClientAreaMismatch
};

/// The one place this tree calls the Win32 window functions. It owns one top-level window, borderless and covering the
/// whole of the primary monitor (ADR-010), and pumps messages without ever blocking the frame.
///
/// There is no size to ask for: the monitor decides. A window with a caption cannot have a client area as tall as the
/// monitor it is on -- the caption and borders are 47 pixels of the 1080 a 1080p display has -- and 1080p is the most
/// common display there is, so a decorated window could never present the screen unscaled on one. Dropping the frame
/// is what gives the client area the monitor's exact pixels, and on a 1920x1080 monitor those are the screen's, which
/// is the 1:1 unfiltered path ADR-009's present step takes.
///
/// A window is created into an out parameter and destroyed with the object; it is neither copied nor moved, because
/// the window procedure holds a pointer to it.
class Window
{
public:
  struct Desc
  {
    // A null-terminated literal, handed to CreateWindowExW as it stands. Not a std::wstring_view: Create is noexcept,
    // and copying a view into a string to null-terminate it is an allocation, which is a throw, which in a noexcept
    // function is std::terminate. R13 makes every title in this game a compile-time literal anyway.
    //
    // A borderless window shows its title nowhere. It is still what the taskbar, Alt+Tab and every debugger name the
    // process by, which is reason enough to keep it.
    const wchar_t* title;
  };

  Window() = default;
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  Window(Window&&) = delete;
  Window& operator=(Window&&) = delete;
  ~Window();

  /// Creates the window, hidden, borderless, at the primary monitor's origin and exactly its size (ADR-010). The game
  /// still draws at 1920x1080 whatever that size is; the frame's last step presents the scene target into the client
  /// area, which on a 1920x1080 monitor is 1:1 and unfiltered and on any other is scaled with the aspect preserved.
  ///
  /// Returns false if the class cannot be registered, Windows will not say how big the primary monitor is, that
  /// monitor has no pixels, the window cannot be created, or the client area did not come out the monitor's size.
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
  /// this is what it got instead of the monitor's size, which is the one number a build agent's log needs.
  [[nodiscard]] std::uint32_t MeasuredWidthPixels() const noexcept
  {
    return m_measuredWidthPixels;
  }

  [[nodiscard]] std::uint32_t MeasuredHeightPixels() const noexcept
  {
    return m_measuredHeightPixels;
  }

  /// Whether the monitor is not 1920x1080, so the present step is scaling rather than copying. False means the client
  /// area is exactly the screen and the frame ends unfiltered; true means the renderer is scaling, and the log says so.
  [[nodiscard]] bool RequiresPresentScale() const noexcept
  {
    return m_requiresPresentScale;
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

  /// Where the window's messages go besides the window procedure, so that input can be read without this class
  /// knowing what input is (NC-024). A plain function pointer and a context: connecting the two allocates nothing,
  /// and Create stays noexcept.
  ///
  /// The sink sees a message BEFORE the procedure's own handling and says whether it took it; a message it takes is
  /// still handled here, because a window must go on answering WM_CLOSE whatever else is listening.
  using MessageSink = bool (*)(void*, UINT, WPARAM, LPARAM);

  void SetMessageSink(MessageSink _sink, void* _context) noexcept
  {
    m_messageSink = _sink;
    m_messageSinkContext = _context;
  }

  /// Asks the window to close, as Alt+F4 does. The next pump reports it. A borderless window has no close box, so
  /// this and Alt+F4 are the whole of the way out; since NC-024, Escape reaches it through InputState and the
  /// executable's own loop rather than through a special case in the window procedure.
  void RequestClose() noexcept;

  [[nodiscard]] bool Closed() const noexcept
  {
    return m_closed;
  }

private:
  static LRESULT CALLBACK WindowProcedure(HWND _handle, UINT _message, WPARAM _wparam, LPARAM _lparam) noexcept;

  HWND m_handle = nullptr;
  MessageSink m_messageSink = nullptr;
  void* m_messageSinkContext = nullptr;
  /// How many mouse buttons are held, so the capture is released when the last one is (NC-024).
  std::uint32_t m_buttonsHeld = 0;
  bool m_closed = false;
  WindowFault m_fault = WindowFault::None;
  bool m_requiresPresentScale = false;
  unsigned long m_systemError = 0;
  std::uint32_t m_measuredWidthPixels = 0;
  std::uint32_t m_measuredHeightPixels = 0;
};

} // namespace Neuron
