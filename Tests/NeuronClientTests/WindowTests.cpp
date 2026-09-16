// Tests/NeuronClientTests/WindowTests.cpp
#include "pch.h"
#include "Window.h"

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

} // namespace

TEST_CLASS(WindowTests)
{
public:
  TEST_METHOD(TheClientAreaIsExactlyTheScreen)
  {
    Neuron::Window window;
    Assert::IsTrue(CreateHidden(window));
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
    Assert::IsTrue(CreateHidden(window));
    const LONG_PTR style = GetWindowLongPtrW(window.Handle(), GWL_STYLE);
    Assert::AreEqual(LONG_PTR{0}, style & WS_THICKFRAME);
    Assert::AreEqual(LONG_PTR{0}, style & WS_MAXIMIZEBOX);
    Assert::AreNotEqual(LONG_PTR{0}, style & WS_SYSMENU);
  }

  TEST_METHOD(PumpingReturnsAtOnceWhileTheWindowIsOpen)
  {
    Neuron::Window window;
    Assert::IsTrue(CreateHidden(window));
    for (int i = 0; i < 100; ++i)
    {
      Assert::IsTrue(window.PumpMessages());
    }
    Assert::IsFalse(window.Closed());
  }

  TEST_METHOD(ClosingIsReportedByTheNextPump)
  {
    Neuron::Window window;
    Assert::IsTrue(CreateHidden(window));
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
      Assert::IsTrue(CreateHidden(first));
    }
    Neuron::Window second;
    Assert::IsTrue(CreateHidden(second));
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    Assert::IsTrue(second.ClientSizePixels(width, height));
    Assert::AreEqual(Neuron::SCREEN_WIDTH_PIXELS, width);
  }
};

} // namespace NeuronClientTests
