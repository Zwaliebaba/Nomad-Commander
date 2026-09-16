// Tests/NeuronClientTests/WindowTests.cpp
#include "pch.h"
#include "Window.h"
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

} // namespace

TEST_CLASS(WindowTests)
{
public:
  TEST_METHOD(TheClientAreaIsExactlyTheScreen)
  {
    Neuron::Window window;
    Assert::IsTrue(CreateHidden(window), WhyItFailed(window).c_str());
    Assert::IsNotNull(window.Handle());

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    Assert::IsTrue(window.ClientSizePixels(width, height));
    Assert::AreEqual(Neuron::SCREEN_WIDTH_PIXELS, width);
    Assert::AreEqual(Neuron::SCREEN_HEIGHT_PIXELS, height);
  }

  TEST_METHOD(TheWindowCannotBeResizedOrMaximized)
  {
    Neuron::Window window;
    Assert::IsTrue(CreateHidden(window), WhyItFailed(window).c_str());
    const LONG_PTR style = GetWindowLongPtrW(window.Handle(), GWL_STYLE);
    Assert::AreEqual(LONG_PTR{0}, style & WS_THICKFRAME);
    Assert::AreEqual(LONG_PTR{0}, style & WS_MAXIMIZEBOX);
    Assert::AreNotEqual(LONG_PTR{0}, style & WS_SYSMENU);
  }

  TEST_METHOD(PumpingReturnsAtOnceWhileTheWindowIsOpen)
  {
    Neuron::Window window;
    Assert::IsTrue(CreateHidden(window), WhyItFailed(window).c_str());
    for (int i = 0; i < 100; ++i)
    {
      Assert::IsTrue(window.PumpMessages());
    }
    Assert::IsFalse(window.Closed());
  }

  TEST_METHOD(ClosingIsReportedByTheNextPump)
  {
    Neuron::Window window;
    Assert::IsTrue(CreateHidden(window), WhyItFailed(window).c_str());
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
      Assert::IsTrue(CreateHidden(first), WhyItFailed(first).c_str());
    }
    Neuron::Window second;
    Assert::IsTrue(CreateHidden(second), WhyItFailed(second).c_str());
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    Assert::IsTrue(second.ClientSizePixels(width, height));
    Assert::AreEqual(Neuron::SCREEN_WIDTH_PIXELS, width);
  }
};

} // namespace NeuronClientTests
