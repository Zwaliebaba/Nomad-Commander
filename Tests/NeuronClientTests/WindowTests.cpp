// Tests/NeuronClientTests/WindowTests.cpp
#include "pch.h"
#include "Window.h"
#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

/// The window is created but never shown: a window can be made without a desktop session to put it on, which is what
/// lets this suite run on the CI runner (NC-020). Under ADR-010 there is no size to ask for -- the monitor decides.
[[nodiscard]] bool CreateHidden(Neuron::Window& _outWindow)
{
  const Neuron::Window::Desc desc{L"NomadCommanderTest"};
  return Neuron::Window::Create(desc, _outWindow);
}

/// What failed, in words, so that a build agent's log names the branch rather than only the line.
///
/// Call it AFTER the creation it describes, in a statement of its own. The order in which a call's arguments are
/// evaluated is unspecified, so `Assert::IsTrue(Create(...), WhyItFailed(...).c_str())` may read the window before it
/// has been created — which is exactly what happened on the first run that used this.
[[nodiscard]] std::wstring WhyItFailed(const Neuron::Window& _window)
{
  std::wstring reason = L"Window::Create failed: ";
  switch (_window.Fault())
  {
  case Neuron::WindowFault::None:
    reason += L"no fault recorded";
    break;
  case Neuron::WindowFault::ClassRegistration:
    reason += L"RegisterClassExW";
    break;
  case Neuron::WindowFault::MonitorQuery:
    reason += L"neither GetMonitorInfoW nor GetSystemMetrics would say how big the primary monitor is";
    break;
  case Neuron::WindowFault::DesktopTooSmall:
    reason += L"the primary monitor reports no pixels";
    break;
  case Neuron::WindowFault::Creation:
    reason += L"CreateWindowExW";
    break;
  case Neuron::WindowFault::ClientAreaMismatch:
    reason += L"the client area came out " + std::to_wstring(_window.MeasuredWidthPixels()) + L"x" +
              std::to_wstring(_window.MeasuredHeightPixels());
    break;
  }
  return reason + L"; GetLastError=" + std::to_wstring(_window.SystemError());
}

/// The primary monitor's size in physical pixels — what ADR-010 makes the client area, whatever the screen is. The
/// tests bound their expectations by this rather than by SCREEN_*_PIXELS, because a build agent's monitor is 1024x768
/// and the screen is 1920x1080: the window there is legitimately not the screen, and the present step scales.
[[nodiscard]] bool PrimaryMonitorSizePixels(std::uint32_t& _outWidth, std::uint32_t& _outHeight)
{
  const POINT origin{0, 0};
  const HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO info{};
  info.cbSize = sizeof info;
  if (monitor == nullptr || GetMonitorInfoW(monitor, &info) == FALSE)
  {
    return false;
  }
  const LONG width = info.rcMonitor.right - info.rcMonitor.left;
  const LONG height = info.rcMonitor.bottom - info.rcMonitor.top;
  if (width <= 0 || height <= 0)
  {
    return false;
  }
  _outWidth = static_cast<std::uint32_t>(width);
  _outHeight = static_cast<std::uint32_t>(height);
  return true;
}

} // namespace

TEST_CLASS(WindowTests)
{
public:
  TEST_METHOD(TheClientAreaIsTheWholeOfThePrimaryMonitor)
  {
    // ADR-010. The client area is the monitor's pixels, so on a 1920x1080 display it is exactly the screen and the
    // present step is a 1:1 copy; on any other it is whatever the monitor has and the scene target is scaled into it.
    Neuron::Window window;
    const bool created = CreateHidden(window);
    Assert::IsTrue(created, WhyItFailed(window).c_str());
    Assert::IsNotNull(window.Handle());

    std::uint32_t monitorWidth = 0;
    std::uint32_t monitorHeight = 0;
    Assert::IsTrue(PrimaryMonitorSizePixels(monitorWidth, monitorHeight), L"no primary monitor to bound the window by");

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    Assert::IsTrue(window.ClientSizePixels(width, height));
    Assert::AreEqual(monitorWidth, width);
    Assert::AreEqual(monitorHeight, height);
  }

  TEST_METHOD(TheWindowHasNoNonClientArea)
  {
    // The independent half of the test above: whatever the monitor turned out to be, the client area is the WHOLE
    // window. That is what removing the caption and the borders bought, and it is what makes the client area able to
    // be as tall as the monitor — the 47 pixels of caption and border at 125% scaling are the reason a decorated
    // window never could be.
    Neuron::Window window;
    const bool created = CreateHidden(window);
    Assert::IsTrue(created, WhyItFailed(window).c_str());

    RECT frame{};
    Assert::AreNotEqual(0, GetWindowRect(window.Handle(), &frame));
    std::uint32_t clientWidth = 0;
    std::uint32_t clientHeight = 0;
    Assert::IsTrue(window.ClientSizePixels(clientWidth, clientHeight));
    Assert::AreEqual(static_cast<std::uint32_t>(frame.right - frame.left), clientWidth, L"the window is wider than its client area");
    Assert::AreEqual(static_cast<std::uint32_t>(frame.bottom - frame.top), clientHeight, L"the window is taller than its client area");
  }

  TEST_METHOD(TheWindowIsBorderlessAndCannotBeResizedOrMaximized)
  {
    Neuron::Window window;
    const bool created = CreateHidden(window);
    Assert::IsTrue(created, WhyItFailed(window).c_str());
    const LONG_PTR style = GetWindowLongPtrW(window.Handle(), GWL_STYLE);
    Assert::AreNotEqual(LONG_PTR{0}, style & WS_POPUP, L"the window is not borderless");
    Assert::AreEqual(LONG_PTR{0}, style & WS_CAPTION);
    Assert::AreEqual(LONG_PTR{0}, style & WS_THICKFRAME);
    Assert::AreEqual(LONG_PTR{0}, style & WS_MAXIMIZEBOX);

    // Not topmost, deliberately (ADR-010): a borderless window covering the screen that also insists on being above
    // everything else is one a player cannot get out from under, and one that fights the debugger.
    const LONG_PTR extendedStyle = GetWindowLongPtrW(window.Handle(), GWL_EXSTYLE);
    Assert::AreEqual(LONG_PTR{0}, extendedStyle & WS_EX_TOPMOST, L"the window is topmost");
  }

  TEST_METHOD(ThePresentScaleIsNeededExactlyWhenTheMonitorIsNotTheScreen)
  {
    Neuron::Window window;
    const bool created = CreateHidden(window);
    Assert::IsTrue(created, WhyItFailed(window).c_str());

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    Assert::IsTrue(window.ClientSizePixels(width, height));
    const bool isTheScreen = width == Neuron::SCREEN_WIDTH_PIXELS && height == Neuron::SCREEN_HEIGHT_PIXELS;
    Assert::AreEqual(!isTheScreen, window.RequiresPresentScale());
  }

  TEST_METHOD(PumpingReturnsAtOnceWhileTheWindowIsOpen)
  {
    Neuron::Window window;
    const bool created = CreateHidden(window);
    Assert::IsTrue(created, WhyItFailed(window).c_str());
    for (int i = 0; i < 100; ++i)
    {
      Assert::IsTrue(window.PumpMessages());
    }
    Assert::IsFalse(window.Closed());
  }

  TEST_METHOD(ClosingIsReportedByTheNextPump)
  {
    Neuron::Window window;
    const bool created = CreateHidden(window);
    Assert::IsTrue(created, WhyItFailed(window).c_str());
    window.RequestClose();

    // The close is posted, so it takes a pump to be seen; the pump that handles it also sees the quit that follows.
    bool open = true;
    for (int i = 0; i < 10 && open; ++i)
    {
      open = window.PumpMessages();
    }
    Assert::IsFalse(open);
    Assert::IsTrue(window.Closed());
    Assert::IsNull(window.Handle());
  }

  TEST_METHOD(ASecondWindowCanBeCreatedAfterTheFirstIsGone)
  {
    // The window class is registered once per process; a second creation must not fail because of it.
    {
      Neuron::Window first;
      const bool createdFirst = CreateHidden(first);
      Assert::IsTrue(createdFirst, WhyItFailed(first).c_str());
    }
    Neuron::Window second;
    const bool createdSecond = CreateHidden(second);
    Assert::IsTrue(createdSecond, WhyItFailed(second).c_str());
    std::uint32_t firstWidth = 0;
    std::uint32_t firstHeight = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    Assert::IsTrue(second.ClientSizePixels(width, height));
    Assert::IsTrue(width > 0 && height > 0);

    // Two windows get the same thing: the size is a function of the monitor, not of history.
    Neuron::Window third;
    const bool createdThird = CreateHidden(third);
    Assert::IsTrue(createdThird, WhyItFailed(third).c_str());
    Assert::IsTrue(third.ClientSizePixels(firstWidth, firstHeight));
    Assert::AreEqual(width, firstWidth);
    Assert::AreEqual(height, firstHeight);
  }
};

} // namespace NeuronClientTests
