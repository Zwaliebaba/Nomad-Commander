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

// A fixed-size window: a caption, a system menu and a minimize box, and deliberately no WS_THICKFRAME and no
// WS_MAXIMIZEBOX, because the screen is 1920x1080 and nothing scales it (R12).
//
// WS_OVERLAPPED is named for what the window is, and contributes nothing: it is zero. AdjustWindowRectExForDpi
// documents that the style must not be specified, which is a statement about a style that has no bits rather than one
// the call could detect; the frame arithmetic below is the same with it and without it.
constexpr DWORD WINDOW_STYLE = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
constexpr DWORD WINDOW_EXTENDED_STYLE = 0;

// The largest window this class will admit to, in pixels, and the reason it has to say so.
//
// CreateWindowExW sends WM_GETMINMAXINFO to any window carrying WS_CAPTION before it returns, and clamps the new
// window to that message's ptMaxTrackSize. The default is SM_CXMAXTRACK by SM_CYMAXTRACK, which is the size of the
// ENTIRE DESKTOP -- so on any desktop smaller than the screen the clamp silently hands back a smaller client area
// than the one asked for, which is exactly the promise this class exists to keep (R12). At 1920x1080 that is most
// desktops, not a corner case. GetSystemMetrics documents the way
// out: "A window can override this value by processing the WM_GETMINMAXINFO message." The procedure below does, and
// this is the value it gives.
//
// Since ADR-009 the fit in Create keeps the window inside the work area, so this override normally has nothing to
// do. It still matters on the one path where the fit cannot run: when Windows will not report a work area, Create
// uses the requested size unchanged, and without this the clamp would silently shrink it again.
//
// Overriding it costs nothing, because a tracking size is a limit on DRAGGING a window's frame and this style carries
// no WS_THICKFRAME: there is no frame to drag, so the clamp at creation is the only thing the number ever did. 32767
// rather than something larger because WM_SIZE packs the client width and height into sixteen bits each, and a window
// wider than a signed short is one whose own size messages cannot describe it.
constexpr LONG MAX_TRACK_PIXELS = 32767;

/// The window rectangle whose client area is exactly the requested pixels at the given scaling.
[[nodiscard]] bool FrameForClientArea(std::uint32_t _clientWidth, std::uint32_t _clientHeight, UINT _dpi, RECT& _outFrame) noexcept
{
  _outFrame.left = 0;
  _outFrame.top = 0;
  _outFrame.right = static_cast<LONG>(_clientWidth);
  _outFrame.bottom = static_cast<LONG>(_clientHeight);
  return AdjustWindowRectExForDpi(&_outFrame, WINDOW_STYLE, FALSE, WINDOW_EXTENDED_STYLE, _dpi) != FALSE;
}

// A size to measure the non-client padding against. A fixed frame's borders and caption are the same thickness
// whatever the client area is, so any probe does; this one is comfortably larger than the padding it measures.
constexpr LONG FRAME_PROBE_PIXELS = 1000;

/// How much wider and taller than its client area a window of this style is, at this scaling.
[[nodiscard]] bool FramePadding(UINT _dpi, LONG& _outWidthPixels, LONG& _outHeightPixels) noexcept
{
  RECT probe{0, 0, FRAME_PROBE_PIXELS, FRAME_PROBE_PIXELS};
  if (AdjustWindowRectExForDpi(&probe, WINDOW_STYLE, FALSE, WINDOW_EXTENDED_STYLE, _dpi) == FALSE)
  {
    return false;
  }
  _outWidthPixels = (probe.right - probe.left) - FRAME_PROBE_PIXELS;
  _outHeightPixels = (probe.bottom - probe.top) - FRAME_PROBE_PIXELS;
  return true;
}

/// The client area this window will actually have: the requested one where the work area can hold a window around it,
/// and otherwise the largest area of the same shape that it can (ADR-009's *fit* policy).
///
/// Bounded by the WORK area rather than the whole desktop, so the whole window is visible and none of it is under the
/// taskbar. If Windows will not say what the work area is, the request is used unchanged -- the same behaviour this
/// class had before ADR-009, and the case the tracking-size override in the window procedure still exists for.
[[nodiscard]] WindowFault FitClientAreaToWorkArea(std::uint32_t _requestedWidth, std::uint32_t _requestedHeight, UINT _dpi,
                                                  std::uint32_t& _outWidth, std::uint32_t& _outHeight) noexcept
{
  _outWidth = _requestedWidth;
  _outHeight = _requestedHeight;

  RECT workArea{};
  if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0) == FALSE)
  {
    return WindowFault::None;
  }
  LONG paddingWidth = 0;
  LONG paddingHeight = 0;
  if (!FramePadding(_dpi, paddingWidth, paddingHeight))
  {
    return WindowFault::FrameArithmetic;
  }

  const LONG availableWidth = (workArea.right - workArea.left) - paddingWidth;
  const LONG availableHeight = (workArea.bottom - workArea.top) - paddingHeight;
  if (availableWidth <= 0 || availableHeight <= 0 || _requestedWidth == 0 || _requestedHeight == 0)
  {
    return WindowFault::DesktopTooSmall;
  }
  if (static_cast<LONG>(_requestedWidth) <= availableWidth && static_cast<LONG>(_requestedHeight) <= availableHeight)
  {
    return WindowFault::None;
  }

  // The largest rectangle of the requested shape that fits, in 64 bits because the products are of two screen extents.
  const std::int64_t requestedWidth = _requestedWidth;
  const std::int64_t requestedHeight = _requestedHeight;
  const std::int64_t heightAtFullWidth = availableWidth * requestedHeight / requestedWidth;
  std::int64_t fittedWidth = availableWidth;
  std::int64_t fittedHeight = heightAtFullWidth;
  if (heightAtFullWidth > availableHeight)
  {
    fittedHeight = availableHeight;
    fittedWidth = availableHeight * requestedWidth / requestedHeight;
  }
  if (fittedWidth <= 0 || fittedHeight <= 0)
  {
    return WindowFault::DesktopTooSmall;
  }
  _outWidth = static_cast<std::uint32_t>(fittedWidth);
  _outHeight = static_cast<std::uint32_t>(fittedHeight);
  return WindowFault::None;
}

/// Where a frame of that size sits, centred on the primary monitor's work area.
void CenterOnPrimaryMonitor(const RECT& _frame, int& _outLeft, int& _outTop) noexcept
{
  const int frameWidth = static_cast<int>(_frame.right - _frame.left);
  const int frameHeight = static_cast<int>(_frame.bottom - _frame.top);
  RECT workArea{};
  if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0) == FALSE)
  {
    _outLeft = CW_USEDEFAULT;
    _outTop = CW_USEDEFAULT;
    return;
  }
  const int workWidth = static_cast<int>(workArea.right - workArea.left);
  const int workHeight = static_cast<int>(workArea.bottom - workArea.top);
  _outLeft = static_cast<int>(workArea.left) + (workWidth - frameWidth) / 2;
  _outTop = static_cast<int>(workArea.top) + (workHeight - frameHeight) / 2;
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
  _outWindow.m_fittedToDesktop = false;

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

  // The system's scaling is the best guess before there is a window to ask; once there is one, its own monitor's
  // scaling decides, and the fit and the frame are both recomputed if the two differ.
  const UINT initialDpi = GetDpiForSystem();
  std::uint32_t clientWidth = 0;
  std::uint32_t clientHeight = 0;
  const WindowFault fitFault =
    FitClientAreaToWorkArea(_desc.clientWidthPixels, _desc.clientHeightPixels, initialDpi, clientWidth, clientHeight);
  if (fitFault != WindowFault::None)
  {
    _outWindow.m_systemError = GetLastError();
    _outWindow.m_fault = fitFault;
    return false;
  }

  RECT frame{};
  if (!FrameForClientArea(clientWidth, clientHeight, initialDpi, frame))
  {
    _outWindow.m_systemError = GetLastError();
    _outWindow.m_fault = WindowFault::FrameArithmetic;
    return false;
  }
  int left = CW_USEDEFAULT;
  int top = CW_USEDEFAULT;
  CenterOnPrimaryMonitor(frame, left, top);

  HWND handle = CreateWindowExW(WINDOW_EXTENDED_STYLE, WINDOW_CLASS_NAME, _desc.title, WINDOW_STYLE, left, top,
                                static_cast<int>(frame.right - frame.left), static_cast<int>(frame.bottom - frame.top), nullptr, nullptr,
                                GetModuleHandleW(nullptr), &_outWindow);
  if (handle == nullptr)
  {
    _outWindow.m_systemError = GetLastError();
    _outWindow.m_fault = WindowFault::Creation;
    return false;
  }
  _outWindow.m_handle = handle;

  // If the window landed on a monitor scaled differently from the system's, the frame it was given is the wrong size
  // for the client area computed above -- and so, since the padding changed, is the fit. Redo both and resize once.
  const UINT windowDpi = GetDpiForWindow(handle);
  if (windowDpi != 0 && windowDpi != initialDpi)
  {
    std::uint32_t refitWidth = 0;
    std::uint32_t refitHeight = 0;
    RECT adjusted{};
    if (FitClientAreaToWorkArea(_desc.clientWidthPixels, _desc.clientHeightPixels, windowDpi, refitWidth, refitHeight) ==
          WindowFault::None &&
        FrameForClientArea(refitWidth, refitHeight, windowDpi, adjusted))
    {
      clientWidth = refitWidth;
      clientHeight = refitHeight;
      SetWindowPos(handle, nullptr, 0, 0, static_cast<int>(adjusted.right - adjusted.left),
                   static_cast<int>(adjusted.bottom - adjusted.top), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
  }
  _outWindow.m_fittedToDesktop = clientWidth != _desc.clientWidthPixels || clientHeight != _desc.clientHeightPixels;

  // The client area is what the renderer scales the scene target into (ADR-009), so it is checked rather than assumed:
  // the size this function computed is the promise, not the size the caller asked for.
  std::uint32_t actualWidth = 0;
  std::uint32_t actualHeight = 0;
  const bool measured = _outWindow.ClientSizePixels(actualWidth, actualHeight);
  _outWindow.m_measuredWidthPixels = actualWidth;
  _outWindow.m_measuredHeightPixels = actualHeight;
  if (!measured || actualWidth != clientWidth || actualHeight != clientHeight)
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

  case WM_KEYDOWN:
    // Escape closes, until NC-024 gives input a home of its own.
    if (_wparam == VK_ESCAPE && window != nullptr)
    {
      window->RequestClose();
      return 0;
    }
    break;

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
