// NeuronClient/Window.cpp
#include "pch.h"
#include "Window.h"
#include "Debug.h"

#include <cstdint>

namespace Neuron
{

namespace
{

// AGENTS.md R1: file-scope state is g_ in an anonymous namespace. The class is registered once per process and stays
// registered for its lifetime; there is no case in this game where it is unregistered and registered again.
const wchar_t* const WINDOW_CLASS_NAME = L"NomadCommanderWindow";
bool g_classRegistered = false;

// A borderless window (ADR-010): no caption, no border, no system menu, and so no non-client area at all. The client
// area is therefore the window rectangle, and setting that rectangle to the monitor's gives a client area of exactly
// the monitor's pixels -- which on a 1920x1080 display is the screen, presented 1:1 and unfiltered.
//
// WS_POPUP is what removes the frame. It is not fullscreen in the exclusive sense: no display mode is changed and no
// swap chain goes fullscreen, so Alt+Tab, the debugger and a second monitor all behave normally, and the window is an
// ordinary unowned top-level one that the taskbar and Alt+Tab list. WS_EX_TOPMOST is deliberately absent, because a
// window that insists on being above everything is one a player cannot get out from under.
//
// The styles that are gone were never doing anything a borderless window can use: WS_CAPTION drew the frame this
// decision removes, WS_MINIMIZEBOX and WS_SYSMENU put buttons on that frame, and there was never a WS_THICKFRAME or a
// WS_MAXIMIZEBOX. Alt+F4 still closes: DefWindowProcW turns it into WM_SYSCOMMAND SC_CLOSE, and the procedure below
// forwards every message it does not handle.
constexpr DWORD WINDOW_STYLE = WS_POPUP;
constexpr DWORD WINDOW_EXTENDED_STYLE = 0;

// The largest window this class will admit to, in pixels, and the reason it still says so.
//
// CreateWindowExW clamps a new window to the ptMaxTrackSize its WM_GETMINMAXINFO reports, whose default is
// SM_CXMAXTRACK by SM_CYMAXTRACK -- the size of the ENTIRE DESKTOP. That clamp cost this class four red CI runs while
// the style carried WS_CAPTION and the client area was the screen's 1920x1080 on a 1024x768 build agent.
//
// Since ADR-010 it has nothing to clamp: the window is exactly one monitor, and the desktop is never smaller than the
// monitor it contains. The override stays anyway, because WM_GETMINMAXINFO is also sent on every later SetWindowPos,
// because it costs one branch in a message this class receives a handful of times, and because rediscovering the
// clamp is expensive and keeping the answer is free. 32767 rather than something larger because WM_SIZE packs the
// client width and height into sixteen bits each, and a window wider than a signed short is one whose own size
// messages cannot describe it.
constexpr LONG MAX_TRACK_PIXELS = 32767;

/// The primary monitor's rectangle in physical pixels -- the whole of it, not the work area, because a borderless
/// window covers the taskbar rather than sitting beside it.
///
/// Physical pixels because the process is per-monitor-v2 DPI aware (the executable's manifest, NC-001). That matters
/// more here than it did under any windowed policy: an unaware process is told a 1920x1080 monitor at 125% scaling is
/// 1536x864, would size itself to that, and Windows would stretch the result -- the blur R12 exists to prevent, on
/// every machine rather than only on a display that cannot hold the screen.
[[nodiscard]] bool PrimaryMonitorRect(RECT& _outRect) noexcept
{
  // The primary monitor's origin is (0,0) by definition, so the point picks it out without a window to ask about.
  const POINT origin{0, 0};
  const HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
  if (monitor != nullptr)
  {
    MONITORINFO info{};
    info.cbSize = sizeof info;
    if (GetMonitorInfoW(monitor, &info) != FALSE)
    {
      _outRect = info.rcMonitor;
      return true;
    }
  }

  // GetSystemMetrics is the primary monitor's size by definition and cannot fail in a way it can report, so it is the
  // fallback rather than the first choice: MONITORINFO is what a second monitor would need, and this is what is left.
  const int widthPixels = GetSystemMetrics(SM_CXSCREEN);
  const int heightPixels = GetSystemMetrics(SM_CYSCREEN);
  if (widthPixels <= 0 || heightPixels <= 0)
  {
    return false;
  }
  _outRect.left = 0;
  _outRect.top = 0;
  _outRect.right = widthPixels;
  _outRect.bottom = heightPixels;
  return true;
}

} // namespace

Window::~Window()
{
  if (m_handle != nullptr)
  {
    SetWindowLongPtrW(m_handle, GWLP_USERDATA, 0);
    DestroyWindow(m_handle);
    m_handle = nullptr;
  }
}

bool Window::Create(const Desc& _desc, Window& _outWindow) noexcept
{
  NOMAD_ASSERT(_outWindow.m_handle == nullptr);
  _outWindow.m_fault = WindowFault::None;
  _outWindow.m_systemError = 0;
  _outWindow.m_measuredWidthPixels = 0;
  _outWindow.m_measuredHeightPixels = 0;
  _outWindow.m_requiresPresentScale = false;

  // Registered once per process, here rather than in a free helper: the window procedure is private, and only a member
  // may take its address.
  if (!g_classRegistered)
  {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof windowClass;
    windowClass.style = 0;
    windowClass.lpfnWndProc = &Window::WindowProcedure;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    // No background brush: the swap chain owns every pixel, and a brush would need GDI, which NOGDI has removed
    // (AGENTS.md §4). WM_ERASEBKGND is answered below instead.
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = WINDOW_CLASS_NAME;
    if (RegisterClassExW(&windowClass) == 0)
    {
      _outWindow.m_systemError = GetLastError();
      _outWindow.m_fault = WindowFault::ClassRegistration;
      return false;
    }
    g_classRegistered = true;
  }

  // The monitor is the whole of the geometry. There is no fit to compute, no frame to adjust and no centring to do:
  // a borderless window at the monitor's origin, of the monitor's size, has a client area of the monitor's pixels.
  RECT monitor{};
  if (!PrimaryMonitorRect(monitor))
  {
    _outWindow.m_systemError = GetLastError();
    _outWindow.m_fault = WindowFault::MonitorQuery;
    return false;
  }
  const LONG monitorWidth = monitor.right - monitor.left;
  const LONG monitorHeight = monitor.bottom - monitor.top;
  if (monitorWidth <= 0 || monitorHeight <= 0)
  {
    _outWindow.m_systemError = GetLastError();
    _outWindow.m_fault = WindowFault::DesktopTooSmall;
    return false;
  }

  HWND handle = CreateWindowExW(WINDOW_EXTENDED_STYLE, WINDOW_CLASS_NAME, _desc.title, WINDOW_STYLE, static_cast<int>(monitor.left),
                                static_cast<int>(monitor.top), static_cast<int>(monitorWidth), static_cast<int>(monitorHeight), nullptr,
                                nullptr, GetModuleHandleW(nullptr), &_outWindow);
  if (handle == nullptr)
  {
    _outWindow.m_systemError = GetLastError();
    _outWindow.m_fault = WindowFault::Creation;
    return false;
  }
  _outWindow.m_handle = handle;
  _outWindow.m_requiresPresentScale =
    static_cast<std::uint32_t>(monitorWidth) != SCREEN_WIDTH_PIXELS || static_cast<std::uint32_t>(monitorHeight) != SCREEN_HEIGHT_PIXELS;

  // The client area is what the renderer presents the scene target into (ADR-009), so it is checked rather than
  // assumed: the monitor's size is the promise, and a client area that is not it means something between this call and
  // the window manager disagreed -- DPI virtualisation being the one that matters, because it is silent.
  std::uint32_t actualWidth = 0;
  std::uint32_t actualHeight = 0;
  const bool measured = _outWindow.ClientSizePixels(actualWidth, actualHeight);
  _outWindow.m_measuredWidthPixels = actualWidth;
  _outWindow.m_measuredHeightPixels = actualHeight;
  if (!measured || actualWidth != static_cast<std::uint32_t>(monitorWidth) || actualHeight != static_cast<std::uint32_t>(monitorHeight))
  {
    _outWindow.m_systemError = GetLastError();
    _outWindow.m_fault = WindowFault::ClientAreaMismatch;
    DestroyWindow(handle);
    _outWindow.m_handle = nullptr;
    _outWindow.m_closed = false;
    return false;
  }
  return true;
}

void Window::Show() noexcept
{
  if (m_handle != nullptr)
  {
    ShowWindow(m_handle, SW_SHOWNORMAL);
    SetForegroundWindow(m_handle);
  }
}

bool Window::ClientSizePixels(std::uint32_t& _outWidth, std::uint32_t& _outHeight) const noexcept
{
  if (m_handle == nullptr)
  {
    return false;
  }
  RECT client{};
  if (GetClientRect(m_handle, &client) == FALSE)
  {
    return false;
  }
  _outWidth = static_cast<std::uint32_t>(client.right - client.left);
  _outHeight = static_cast<std::uint32_t>(client.bottom - client.top);
  return true;
}

void Window::RequestClose() noexcept
{
  if (m_handle != nullptr)
  {
    PostMessageW(m_handle, WM_CLOSE, 0, 0);
  }
}

bool Window::PumpMessages() noexcept
{
  MSG message{};
  while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
  {
    if (message.message == WM_QUIT)
    {
      // Nothing in this tree posts one, but a quit belongs to the thread and something else may: honour it.
      m_closed = true;
      return false;
    }
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  return !m_closed;
}

LRESULT CALLBACK Window::WindowProcedure(HWND _handle, UINT _message, WPARAM _wparam, LPARAM _lparam) noexcept
{
  // The instance is handed over in the creation parameters and kept in the window's own storage from then on.
  if (_message == WM_NCCREATE)
  {
    // NOLINTNEXTLINE(performance-no-int-to-ptr) -- WM_NCCREATE's lParam IS a CREATESTRUCTW*; see ADR-007.
    const CREATESTRUCTW* const creation = reinterpret_cast<const CREATESTRUCTW*>(_lparam);
    SetWindowLongPtrW(_handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(creation->lpCreateParams));
    return DefWindowProcW(_handle, _message, _wparam, _lparam);
  }

  // NOLINTNEXTLINE(performance-no-int-to-ptr) -- GWLP_USERDATA round-trips the pointer stored above; see ADR-007.
  Window* const window = reinterpret_cast<Window*>(GetWindowLongPtrW(_handle, GWLP_USERDATA));

  // Input first, and it never stops a message reaching the handling below: since ADR-010 removed the close box,
  // Escape is the way out of this window, and NC-024 moved that key from this procedure into InputState. A sink that
  // swallowed WM_CLOSE would be a window that cannot be closed at all.
  if (window != nullptr && window->m_messageSink != nullptr)
  {
    (void)window->m_messageSink(window->m_messageSinkContext, _message, _wparam, _lparam);
  }

  switch (_message)
  {
  case WM_CLOSE:
    DestroyWindow(_handle);
    return 0;

  case WM_DESTROY:
    // No PostQuitMessage: a quit is a THREAD-wide message, so posting one here would end the message pump of every
    // other window on this thread, and would outlive the window that posted it. PumpMessages reports this window's own
    // closure through m_closed instead, which is what the executable's loop and the tests both read.
    if (window != nullptr)
    {
      window->m_closed = true;
      window->m_handle = nullptr;
    }
    return 0;

  case WM_GETMINMAXINFO:
  {
    // Sent before WM_NCCREATE, so there is no instance to read here yet -- and none is needed: the answer is the same
    // for every window of this class, which is that the desktop's size does not limit it. The default procedure fills
    // the structure in first, and this raises the one member that would otherwise shrink the client area the caller
    // asked for (see MAX_TRACK_PIXELS).
    DefWindowProcW(_handle, _message, _wparam, _lparam);
    // NOLINTNEXTLINE(performance-no-int-to-ptr) -- WM_GETMINMAXINFO's lParam IS a MINMAXINFO*; see ADR-007.
    MINMAXINFO* const limits = reinterpret_cast<MINMAXINFO*>(_lparam);
    if (limits != nullptr)
    {
      limits->ptMaxTrackSize.x = MAX_TRACK_PIXELS;
      limits->ptMaxTrackSize.y = MAX_TRACK_PIXELS;
    }
    return 0;
  }

  case WM_LBUTTONDOWN:
  case WM_RBUTTONDOWN:
  case WM_MBUTTONDOWN:
    // Capture, so a drag that leaves the window still delivers its release here rather than to whatever it passed
    // over. Since ADR-010 the window is the whole monitor, so this only bites on a second one — which is exactly the
    // case nobody would think to test.
    if (window != nullptr)
    {
      ++window->m_buttonsHeld;
      SetCapture(_handle);
    }
    break;

  case WM_LBUTTONUP:
  case WM_RBUTTONUP:
  case WM_MBUTTONUP:
    if (window != nullptr && window->m_buttonsHeld > 0)
    {
      --window->m_buttonsHeld;
      if (window->m_buttonsHeld == 0)
      {
        ReleaseCapture();
      }
    }
    break;

  case WM_ERASEBKGND:
    // Nothing erases the background: the swap chain writes every pixel every frame (R12), and there is no brush.
    return 1;

  case WM_PAINT:
    // Answered without painting, so that Windows stops asking. Nothing here draws; the renderer does, on its own clock.
    ValidateRect(_handle, nullptr);
    return 0;

  default:
    break;
  }
  return DefWindowProcW(_handle, _message, _wparam, _lparam);
}

} // namespace Neuron
