// Tests/NeuronClientTests/InputStateTests.cpp
#include "pch.h"
#include "InputState.h"
#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

/// A client-space point packed the way Windows packs one into an lParam.
[[nodiscard]] LPARAM PackPoint(std::int16_t _x, std::int16_t _y)
{
  return static_cast<LPARAM>((static_cast<std::uint32_t>(static_cast<std::uint16_t>(_y)) << 16) | static_cast<std::uint16_t>(_x));
}

/// A WM_KEYDOWN lParam with bit 30 set, which is how Windows says "this key was already down": a hardware repeat.
constexpr LPARAM KEY_REPEAT_LPARAM = LPARAM{1} << 30;

} // namespace

TEST_CLASS(InputStateTests)
{
public:
  TEST_METHOD(AClickHeldAcrossThreeFramesPressesOnceAndReleasesOnce)
  {
    // The acceptance criterion, exactly: Pressed once, Down three times, Released once. This is the property every
    // immediate-mode widget rests on, because it is what lets a button ask "was I clicked" with no state of its own.
    Neuron::InputState input;
    int pressed = 0;
    int down = 0;
    int released = 0;

    // Frame 1: the button goes down during the pump.
    input.BeginFrame();
    Assert::IsTrue(input.HandleMessage(WM_LBUTTONDOWN, 0, PackPoint(100, 50)));
    pressed += input.MousePressed(Neuron::MouseButton::Left) ? 1 : 0;
    down += input.MouseDown(Neuron::MouseButton::Left) ? 1 : 0;
    released += input.MouseReleased(Neuron::MouseButton::Left) ? 1 : 0;

    // Frames 2 and 3: nothing arrives, and the button is still held.
    for (int frame = 0; frame < 2; ++frame)
    {
      input.BeginFrame();
      pressed += input.MousePressed(Neuron::MouseButton::Left) ? 1 : 0;
      down += input.MouseDown(Neuron::MouseButton::Left) ? 1 : 0;
      released += input.MouseReleased(Neuron::MouseButton::Left) ? 1 : 0;
    }

    // Frame 4: the release.
    input.BeginFrame();
    Assert::IsTrue(input.HandleMessage(WM_LBUTTONUP, 0, PackPoint(100, 50)));
    pressed += input.MousePressed(Neuron::MouseButton::Left) ? 1 : 0;
    down += input.MouseDown(Neuron::MouseButton::Left) ? 1 : 0;
    released += input.MouseReleased(Neuron::MouseButton::Left) ? 1 : 0;

    Assert::AreEqual(1, pressed, L"Pressed must fire exactly once for one click");
    Assert::AreEqual(3, down, L"Down must hold for the three frames the button was held");
    Assert::AreEqual(1, released, L"Released must fire exactly once");
  }

  TEST_METHOD(AClickAndReleaseInsideOneFrameAreBothReported)
  {
    // The reason the edges are set by the messages rather than derived from comparing two frames. At 144 Hz a fast
    // click really does fit inside a frame, and comparing last frame's state to this one would swallow it entirely.
    Neuron::InputState input;
    input.BeginFrame();
    Assert::IsTrue(input.HandleMessage(WM_LBUTTONDOWN, 0, PackPoint(10, 10)));
    Assert::IsTrue(input.HandleMessage(WM_LBUTTONUP, 0, PackPoint(10, 10)));

    Assert::IsTrue(input.MousePressed(Neuron::MouseButton::Left), L"the press was swallowed");
    Assert::IsTrue(input.MouseReleased(Neuron::MouseButton::Left), L"the release was swallowed");
    Assert::IsFalse(input.MouseDown(Neuron::MouseButton::Left), L"the button should not still be down");
  }

  TEST_METHOD(TheMousePositionIsInClientPixelsAndTracksMovement)
  {
    Neuron::InputState input;
    input.BeginFrame();
    Assert::IsTrue(input.HandleMessage(WM_MOUSEMOVE, 0, PackPoint(640, 360)));
    Assert::AreEqual(640, input.MousePosition().xPixels);
    Assert::AreEqual(360, input.MousePosition().yPixels);
    // The first position of all reports no movement, so nothing jumps on the frame the cursor arrives.
    Assert::AreEqual(0, input.MouseDelta().xPixels);

    input.BeginFrame();
    Assert::IsTrue(input.HandleMessage(WM_MOUSEMOVE, 0, PackPoint(650, 350)));
    Assert::AreEqual(650, input.MousePosition().xPixels);
    Assert::AreEqual(10, input.MouseDelta().xPixels);
    Assert::AreEqual(-10, input.MouseDelta().yPixels);

    // A captured drag leaves the window, and those coordinates are negative rather than enormous.
    input.BeginFrame();
    Assert::IsTrue(input.HandleMessage(WM_MOUSEMOVE, 0, PackPoint(-20, -5)));
    Assert::AreEqual(-20, input.MousePosition().xPixels);
    Assert::AreEqual(-5, input.MousePosition().yPixels);
  }

  TEST_METHOD(LosingFocusClearsEverythingHeldAndReportsTheRelease)
  {
    // The criterion is that nothing sticks down. The release edge goes with it, because a widget holding a drag needs
    // to be told the drag ended, not merely to notice later that the button is no longer down.
    Neuron::InputState input;
    input.BeginFrame();
    Assert::IsTrue(input.HandleMessage(WM_LBUTTONDOWN, 0, PackPoint(5, 5)));
    Assert::IsTrue(input.HandleMessage(WM_KEYDOWN, VK_SHIFT, 0));
    Assert::IsTrue(input.MouseDown(Neuron::MouseButton::Left));
    Assert::IsTrue(input.KeyDown(VK_SHIFT));

    input.BeginFrame();
    Assert::IsTrue(input.HandleMessage(WM_KILLFOCUS, 0, 0));
    Assert::IsFalse(input.HasFocus());
    Assert::IsFalse(input.MouseDown(Neuron::MouseButton::Left), L"the mouse button stuck down across focus loss");
    Assert::IsFalse(input.KeyDown(VK_SHIFT), L"the key stuck down across focus loss");
    Assert::IsTrue(input.MouseReleased(Neuron::MouseButton::Left), L"the held button's release was not reported");
    Assert::IsTrue(input.KeyReleased(VK_SHIFT), L"the held key's release was not reported");

    input.BeginFrame();
    Assert::IsTrue(input.HandleMessage(WM_SETFOCUS, 0, 0));
    Assert::IsTrue(input.HasFocus());
  }

  TEST_METHOD(AHeldKeyRepeatsWithoutPressingAgain)
  {
    // Windows repeats a held key by sending WM_KEYDOWN over and over with bit 30 set. A repeat is not a new press,
    // and a UI that treated it as one would advance a field by ten every time somebody leaned on a key.
    Neuron::InputState input;
    input.BeginFrame();
    Assert::IsTrue(input.HandleMessage(WM_KEYDOWN, 'A', 0));
    Assert::IsTrue(input.KeyPressed('A'));
    Assert::IsTrue(input.KeyDown('A'));

    input.BeginFrame();
    Assert::IsTrue(input.HandleMessage(WM_KEYDOWN, 'A', KEY_REPEAT_LPARAM));
    Assert::IsFalse(input.KeyPressed('A'), L"a hardware repeat must not read as a new press");
    Assert::IsTrue(input.KeyDown('A'), L"a repeating key is still down");

    input.BeginFrame();
    Assert::IsTrue(input.HandleMessage(WM_KEYUP, 'A', 0));
    Assert::IsTrue(input.KeyReleased('A'));
    Assert::IsFalse(input.KeyDown('A'));
  }

  TEST_METHOD(EscapeIsAnOrdinaryKeyHere)
  {
    // ADR-010 made Escape the way out of a borderless window, and NC-024 moved it from the window procedure into the
    // executable's loop. It is not special in this class, and that is the point: this is what Main.cpp now reads.
    Neuron::InputState input;
    input.BeginFrame();
    Assert::IsFalse(input.KeyPressed(VK_ESCAPE));
    Assert::IsTrue(input.HandleMessage(WM_KEYDOWN, VK_ESCAPE, 0));
    Assert::IsTrue(input.KeyPressed(VK_ESCAPE));

    input.BeginFrame();
    Assert::IsFalse(input.KeyPressed(VK_ESCAPE), L"the press edge must not survive into the next frame");
    Assert::IsTrue(input.KeyDown(VK_ESCAPE));
  }

  TEST_METHOD(TypedCharactersAndTheWheelLastOneFrame)
  {
    Neuron::InputState input;
    input.BeginFrame();
    Assert::IsTrue(input.HandleMessage(WM_CHAR, 'K', 0));
    Assert::IsTrue(input.HandleMessage(WM_CHAR, '9', 0));
    Assert::IsTrue(input.HandleMessage(WM_MOUSEWHEEL, static_cast<WPARAM>(WHEEL_DELTA) << 16, 0));

    const std::u16string_view typed = input.TypedCharacters();
    Assert::AreEqual(std::size_t{2}, typed.size());
    Assert::IsTrue(typed[0] == u'K' && typed[1] == u'9');
    Assert::AreEqual(1, input.WheelDelta());
    Assert::IsFalse(input.TypingOverflowed());

    input.BeginFrame();
    Assert::IsTrue(input.TypedCharacters().empty(), L"typing must not carry into the next frame");
    Assert::AreEqual(0, input.WheelDelta(), L"the wheel must not carry into the next frame");
  }

  TEST_METHOD(TypingBeyondTheBufferIsDroppedAndReported)
  {
    // The desk has a few numeric fields, not a text editor. Overflow drops the character and says so, rather than
    // writing past the buffer or quietly growing one.
    Neuron::InputState input;
    input.BeginFrame();
    for (std::size_t index = 0; index < Neuron::InputState::MAX_TYPED_CHARACTERS + 8; ++index)
    {
      Assert::IsTrue(input.HandleMessage(WM_CHAR, 'x', 0));
    }
    Assert::AreEqual(Neuron::InputState::MAX_TYPED_CHARACTERS, input.TypedCharacters().size());
    Assert::IsTrue(input.TypingOverflowed());
  }

  TEST_METHOD(AMessageThisDoesNotCareAboutIsNotClaimed)
  {
    // The sink's answer is what the window forwards, so claiming a message this class knows nothing about would be a
    // lie to whatever else is listening.
    Neuron::InputState input;
    Assert::IsFalse(input.HandleMessage(WM_PAINT, 0, 0));
    Assert::IsFalse(input.HandleMessage(WM_CLOSE, 0, 0));
    Assert::IsTrue(input.HandleMessage(WM_MOUSEMOVE, 0, PackPoint(1, 1)));
  }

  TEST_METHOD(TheSinkReachesTheInstanceItWasGiven)
  {
    Neuron::InputState input;
    Assert::IsTrue(Neuron::InputState::MessageSink(&input, WM_LBUTTONDOWN, 0, PackPoint(7, 8)));
    Assert::IsTrue(input.MousePressed(Neuron::MouseButton::Left));
    Assert::AreEqual(7, input.MousePosition().xPixels);
    Assert::IsFalse(Neuron::InputState::MessageSink(nullptr, WM_LBUTTONDOWN, 0, 0), L"a null context must be refused");
  }
};

} // namespace NeuronClientTests
