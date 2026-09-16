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
/// lets this suite run on the CI runner (NC-020).
[[nodiscard]] bool CreateHidden(Neuron::Window& _outWindow)
{
  const Neuron::Window::Desc desc{Neuron::SCREEN_WIDTH_PIXELS, Neuron::SCREEN_HEIGHT_PIXELS, L"NomadCommanderTest"};
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
  case Neuron::WindowFault::FrameArithmetic:
    reason += L"AdjustWindowRectExForDpi";
    break;
  case Neuron::WindowFault::DesktopTooSmall:
    reason += L"the work area cannot hold a window of any size";
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

/// The client area the work area could hold for a window of this style, at the system's scaling. The tests below
/// bound their expectations by this rather than by the screen, because the CI runner's desktop is 1024x768 and the
/// screen is 1920x1080: under ADR-009's fit policy the window there is legitimately smaller than the screen.
[[nodiscard]] bool LargestClientAreaTheWorkAreaHolds(std::uint32_t& _outWidth, std::uint32_t& _outHeight)
{
  RECT workArea{};
  if (SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0) == FALSE)
  {
    return false;
  }
  RECT probe{0, 0, 1000, 1000};
  constexpr DWORD STYLE = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  if (AdjustWindowRectExForDpi(&probe, STYLE, FALSE, 0, GetDpiForSystem()) == FALSE)
  {
    return false;
  }
  const LONG paddingWidth = (probe.right - probe.left) - 1000;
  const LONG paddingHeight = (probe.bottom - probe.top) - 1000;
  const LONG width = (workArea.right - workArea.left) - paddingWidth;
  const LONG height = (workArea.bottom - workArea.top) - paddingHeight;
  if (width <= 0 || height <= 0)
  {
    return false;
  }
  _outWidth = static_cast<std::uint32_t>(width);
  _outHeight = static_cast<std::uint32_t>(height);
  return true;
}

/// The fit keeps the requested shape, to within the pixel that integer arithmetic costs.
[[nodiscard]] bool AspectIsPreserved(std::uint32_t _requestedWidth, std::uint32_t _requestedHeight, std::uint32_t _actualWidth,
                                     std::uint32_t _actualHeight)
{
  const std::int64_t cross =
    static_cast<std::int64_t>(_actualWidth) * _requestedHeight - static_cast<std::int64_t>(_actualHeight) * _requestedWidth;
  const std::int64_t tolerance = static_cast<std::int64_t>(_requestedWidth) + _requestedHeight;
  return cross <= tolerance && -cross <= tolerance;
}

} // namespace

TEST_CLASS(WindowTests)
{
public:
  TEST_METHOD(TheClientAreaIsTheScreenOrTheLargestOfItsShapeThatFits)
  {
    // ADR-009's fit policy, and both branches assert. On a desktop that can hold the screen the client area is exactly
    // the screen and the present scale is 1:1 -- the contract this class had before. On one that cannot, which is the
    // CI runner at 1024x768 and most 1080p laptops, it is the largest area of the same shape the work area holds, and
    // the renderer scales the 1920x1080 scene target into it.
    Neuron::Window window;
    const bool created = CreateHidden(window);
    Assert::IsTrue(created, WhyItFailed(window).c_str());
    Assert::IsNotNull(window.Handle());

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    Assert::IsTrue(window.ClientSizePixels(width, height));

    std::uint32_t availableWidth = 0;
    std::uint32_t availableHeight = 0;
    Assert::IsTrue(LargestClientAreaTheWorkAreaHolds(availableWidth, availableHeight), L"no work area to bound the fit by");

    if (availableWidth >= Neuron::SCREEN_WIDTH_PIXELS && availableHeight >= Neuron::SCREEN_HEIGHT_PIXELS)
    {
      Assert::AreEqual(Neuron::SCREEN_WIDTH_PIXELS, width);
      Assert::AreEqual(Neuron::SCREEN_HEIGHT_PIXELS, height);
      Assert::IsFalse(window.FittedToDesktop());
    }
    else
    {
      Assert::IsTrue(window.FittedToDesktop());
      Assert::IsTrue(width > 0 && height > 0);
      Assert::IsTrue(width <= availableWidth, L"the fitted width does not fit the work area");
      Assert::IsTrue(height <= availableHeight, L"the fitted height does not fit the work area");
      Assert::IsTrue(width == availableWidth || height == availableHeight, L"the fit is not the largest that fits");
      Assert::IsTrue(AspectIsPreserved(Neuron::SCREEN_WIDTH_PIXELS, Neuron::SCREEN_HEIGHT_PIXELS, width, height),
                     L"the fit did not keep the screen's shape");
    }
  }

  TEST_METHOD(TheWindowCannotBeResizedOrMaximized)
  {
    Neuron::Window window;
    const bool created = CreateHidden(window);
    Assert::IsTrue(created, WhyItFailed(window).c_str());
    const LONG_PTR style = GetWindowLongPtrW(window.Handle(), GWL_STYLE);
    Assert::AreEqual(LONG_PTR{0}, style & WS_THICKFRAME);
    Assert::AreEqual(LONG_PTR{0}, style & WS_MAXIMIZEBOX);
    Assert::AreNotEqual(LONG_PTR{0}, style & WS_SYSMENU);
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

  TEST_METHOD(AClientAreaLargerThanTheDesktopIsFittedWithItsShapeKept)
  {
    // The regression this suite was red on for three rounds. CreateWindowExW clamps a new WS_CAPTION window to the
    // desktop-sized default in WM_GETMINMAXINFO's ptMaxTrackSize: on the CI runner's 1024x768 desktop the 1280-wide
    // client area of the day came back 1028 wide, while the height, which fitted, came back right. The screen is
    // 1920x1080 now, so the five tests above overshoot that desktop on both axes rather than one. This test overshoots
    // whatever the desktop is, by reading its own maximum, so the override is exercised on a developer's machine too.
    std::uint32_t availableWidth = 0;
    std::uint32_t availableHeight = 0;
    Assert::IsTrue(LargestClientAreaTheWorkAreaHolds(availableWidth, availableHeight), L"no work area to bound the fit by");
    const std::uint32_t overWideClientPixels = availableWidth + 64u;

    Neuron::Window window;
    const Neuron::Window::Desc desc{overWideClientPixels, Neuron::SCREEN_HEIGHT_PIXELS, L"NomadCommanderTest"};
    const bool created = Neuron::Window::Create(desc, window);
    Assert::IsTrue(created, WhyItFailed(window).c_str());
    Assert::IsTrue(window.FittedToDesktop());

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    Assert::IsTrue(window.ClientSizePixels(width, height));
    Assert::IsTrue(width <= availableWidth, L"the fitted width does not fit the work area");
    Assert::IsTrue(height <= availableHeight, L"the fitted height does not fit the work area");
    Assert::IsTrue(width == availableWidth || height == availableHeight, L"the fit is not the largest that fits");
    Assert::IsTrue(AspectIsPreserved(overWideClientPixels, Neuron::SCREEN_HEIGHT_PIXELS, width, height),
                   L"the fit did not keep the requested shape");
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

    // Two windows asked for the same thing get the same thing: the fit is a function of the desktop, not of history.
    Neuron::Window third;
    const bool createdThird = CreateHidden(third);
    Assert::IsTrue(createdThird, WhyItFailed(third).c_str());
    Assert::IsTrue(third.ClientSizePixels(firstWidth, firstHeight));
    Assert::AreEqual(width, firstWidth);
    Assert::AreEqual(height, firstHeight);
  }
};

} // namespace NeuronClientTests
