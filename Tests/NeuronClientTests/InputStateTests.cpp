// Tests/NeuronClientTests/InputStateTests.cpp
#include "pch.h"
#include "InputState.h"
#include "PresentPass.h"
#include "Window.h"
#include <algorithm>
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

/// Tells the mouse where the present step puts the scene on a client area of this size -- the call the executable
/// makes, through the same Fit, so what is tested is the placement a player on that display really gets (NC-033).
void PlaceForDisplay(Neuron::InputState& _input, std::uint32_t _clientWidthPixels, std::uint32_t _clientHeightPixels)
{
  _input.SetScenePlacement(
    Neuron::PresentPass::Fit(Neuron::SCREEN_WIDTH_PIXELS, Neuron::SCREEN_HEIGHT_PIXELS, _clientWidthPixels, _clientHeightPixels),
    Neuron::SCREEN_WIDTH_PIXELS, Neuron::SCREEN_HEIGHT_PIXELS);
}

/// Where the mouse reads after it moves to a client-space point, in a frame of its own.
[[nodiscard]] Neuron::MousePoint MovedTo(Neuron::InputState& _input, std::int32_t _xPixels, std::int32_t _yPixels)
{
  _input.BeginFrame();
  Assert::IsTrue(
    _input.HandleMessage(WM_MOUSEMOVE, 0, PackPoint(static_cast<std::int16_t>(_xPixels), static_cast<std::int16_t>(_yPixels))));
  return _input.MousePosition();
}

/// One axis of a display, every client pixel of it: the scene pixel the mouse reports must be the one whose footprint
/// on the glass holds that pixel's centre, and a pixel outside the placement must read outside the scene.
///
/// The footprint is the forward mapping -- the present step's viewport puts scene pixel p over client pixels
/// [origin + p * extent / scene, origin + (p + 1) * extent / scene) -- so this checks the way back against the way out
/// rather than against a copy of itself. Doubled throughout, to keep the half-pixel centre an integer.
void CheckEveryPixelOfTheAxis(Neuron::InputState& _input, bool _horizontal, std::uint32_t _clientExtent, std::int32_t _origin,
                              std::uint32_t _placedExtent, std::uint32_t _sceneExtent, std::int32_t _otherAxis, const std::wstring& _where)
{
  std::int32_t lowest = static_cast<std::int32_t>(_sceneExtent);
  std::int32_t highest = -1;
  for (std::int32_t client = 0; client < static_cast<std::int32_t>(_clientExtent); ++client)
  {
    const Neuron::MousePoint reading = _horizontal ? MovedTo(_input, client, _otherAxis) : MovedTo(_input, _otherAxis, client);
    const std::int32_t scene = _horizontal ? reading.xPixels : reading.yPixels;
    const bool inside = client >= _origin && client < _origin + static_cast<std::int32_t>(_placedExtent);
    const std::int64_t twiceCenter = (2 * (static_cast<std::int64_t>(client) - _origin) + 1) * _sceneExtent;
    const bool underCenter = 2 * static_cast<std::int64_t>(scene) * _placedExtent <= twiceCenter &&
                             twiceCenter < 2 * (static_cast<std::int64_t>(scene) + 1) * _placedExtent;
    const bool inScene = scene >= 0 && scene < static_cast<std::int32_t>(_sceneExtent);
    const bool wrong = inside ? !(inScene && underCenter) : inScene;
    if (wrong)
    {
      Assert::Fail(
        (L"client pixel " + std::to_wstring(client) + L" read as scene pixel " + std::to_wstring(scene) + L", " + _where).c_str());
    }
    if (inside)
    {
      lowest = std::min(lowest, scene);
      highest = std::max(highest, scene);
    }
  }
  // The corners of the picture are the corners of the scene: the whole desk can be reached, at every one of these.
  Assert::AreEqual(0, lowest, (L"the scene's first pixel cannot be reached, " + _where).c_str());
  Assert::AreEqual(static_cast<std::int32_t>(_sceneExtent) - 1, highest, (L"the scene's last pixel cannot be reached, " + _where).c_str());
}

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

  TEST_METHOD(UnplacedTheMouseReadsAsWindowsSentItAndTracksMovement)
  {
    // No placement is ADR-009's 1:1 case, where a client pixel is a scene pixel. The executable always places the scene
    // (NC-033); the tests after this one say what that changes, and this one what it leaves alone.
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

  TEST_METHOD(OnAThreeByTwoPanelThePointerReadsTheScenePixelUnderIt)
  {
    // The defect as the owner found it (NC-033). A 3:2 panel is letterboxed: Fit puts the scene 2256x1269, 117 pixels
    // down. The pointer at client (710, 418) is over scene pixel (604, 256) -- and before NC-033 it read (710, 418),
    // which is where NC-024's crosshair was drawn: about 3 cm below and to the right of the pointer, and every click
    // with it.
    const Neuron::PresentPass::Placement placement =
      Neuron::PresentPass::Fit(Neuron::SCREEN_WIDTH_PIXELS, Neuron::SCREEN_HEIGHT_PIXELS, 2256, 1504);
    Assert::AreEqual(0, placement.leftPixels);
    Assert::AreEqual(117, placement.topPixels);
    Assert::AreEqual(2256u, placement.widthPixels);
    Assert::AreEqual(1269u, placement.heightPixels);

    Neuron::InputState input;
    PlaceForDisplay(input, 2256, 1504);
    const Neuron::MousePoint pointer = MovedTo(input, 710, 418);
    Assert::AreEqual(604, pointer.xPixels);
    Assert::AreEqual(256, pointer.yPixels);

    // The picture's corners are the scene's corners, and the bars above and below the picture are not the scene.
    const Neuron::MousePoint topLeft = MovedTo(input, 0, 117);
    Assert::AreEqual(0, topLeft.xPixels);
    Assert::AreEqual(0, topLeft.yPixels);
    const Neuron::MousePoint bottomRight = MovedTo(input, 2255, 117 + 1269 - 1);
    Assert::AreEqual(1919, bottomRight.xPixels);
    Assert::AreEqual(1079, bottomRight.yPixels);
    Assert::AreEqual(-1, MovedTo(input, 0, 116).yPixels, L"the top bar's last row read as the scene's first");
    Assert::AreEqual(1080, MovedTo(input, 0, 117 + 1269).yPixels, L"the bottom bar's first row read as the scene's last");
  }

  TEST_METHOD(BelowTheScreenTheWholeDeskCanStillBeClicked)
  {
    // The half of the defect a larger display hides. On 1366x768 the scene is shown at 0.71x (ADR-009's third column),
    // so a raw client coordinate never passes 1365 -- and before NC-033 the right 554 pixels and the bottom 312 of
    // the desk could not be pointed at at all. The last pixel of the picture is the scene's last pixel now.
    Neuron::InputState input;
    PlaceForDisplay(input, 1366, 768);
    const Neuron::MousePoint topLeft = MovedTo(input, 0, 0);
    Assert::AreEqual(0, topLeft.xPixels);
    Assert::AreEqual(0, topLeft.yPixels);
    const Neuron::MousePoint bottomRight = MovedTo(input, 1364, 767);
    Assert::AreEqual(1919, bottomRight.xPixels);
    Assert::AreEqual(1079, bottomRight.yPixels);

    // Fit leaves one bar column on the right at this size (ADR-009's measurements), and it is not the scene.
    Assert::AreEqual(1920, MovedTo(input, 1365, 0).xPixels, L"the bar column read as the scene's last column");
  }

  TEST_METHOD(AtTheScreenItselfPlacingTheSceneChangesNothing)
  {
    // A 1920x1080 display is ADR-009's copy path and the one every desktop run before NC-033 was made on. The way back
    // is the identity there, for every coordinate -- negative ones under capture and ones past the edge included -- so
    // NC-024's measured (800, 400) still reads (800, 400).
    Neuron::InputState input;
    PlaceForDisplay(input, Neuron::SCREEN_WIDTH_PIXELS, Neuron::SCREEN_HEIGHT_PIXELS);
    const Neuron::MousePoint measured = MovedTo(input, 800, 400);
    Assert::AreEqual(800, measured.xPixels);
    Assert::AreEqual(400, measured.yPixels);
    for (std::int32_t client = -64; client < 2048; ++client)
    {
      const Neuron::MousePoint reading = MovedTo(input, client, client);
      if (reading.xPixels != client || reading.yPixels != client)
      {
        Assert::Fail((L"1:1 moved client pixel " + std::to_wstring(client)).c_str());
      }
    }
  }

  TEST_METHOD(MovementIsMeasuredInScenePixels)
  {
    // At an exact 2x (ADR-009's second case) a scene pixel is a 2x2 block of client pixels, so a move of (200, -100) on
    // the glass is a move of (100, -50) in the scene. A drag measured in client pixels would run twice as fast as the
    // thing being dragged.
    Neuron::InputState input;
    PlaceForDisplay(input, 3840, 2160);
    const Neuron::MousePoint from = MovedTo(input, 1000, 1000);
    Assert::AreEqual(500, from.xPixels);
    Assert::AreEqual(500, from.yPixels);
    const Neuron::MousePoint to = MovedTo(input, 1200, 900);
    Assert::AreEqual(600, to.xPixels);
    Assert::AreEqual(450, to.yPixels);
    Assert::AreEqual(100, input.MouseDelta().xPixels);
    Assert::AreEqual(-50, input.MouseDelta().yPixels);
  }

  TEST_METHOD(APointBeforeTheSceneLandsOutsideItNotOnItsEdge)
  {
    // A pointer in the bar, or a drag carried past the window's edge under capture, is before the scene's first pixel.
    // C++ division rounds towards zero, which would fold it onto that first pixel -- and a widget on the scene's edge
    // would answer to a pointer that is not on it. The way back rounds towards negative infinity instead.
    Neuron::InputState input;
    PlaceForDisplay(input, 2256, 1504);
    const Neuron::MousePoint justOutside = MovedTo(input, -1, 116);
    Assert::AreEqual(-1, justOutside.xPixels, L"the column left of the window read as the scene's first column");
    Assert::AreEqual(-1, justOutside.yPixels, L"the top bar's last row read as the scene's first row");

    // Released out there, the release still happens -- and it happens outside the scene.
    Assert::IsTrue(input.HandleMessage(WM_LBUTTONUP, 0, PackPoint(-400, -300)));
    Assert::IsTrue(input.MousePosition().xPixels < 0 && input.MousePosition().yPixels < 0,
                   L"a release past the corner landed in the scene");
  }

  TEST_METHOD(EveryPixelOfARealDisplayReadsAsTheScenePixelItShows)
  {
    // Twelve display shapes a player could have: below the screen, the screen, 16:10, 3:2 (the owner's, NC-033), 1440p,
    // ultrawide (bars at the sides rather than above and below), and 4K at both 16:9 and 16:10. Every client pixel of
    // both axes is taken through the mouse and checked against the scene pixel whose footprint holds its centre.
    struct Display
    {
      std::uint32_t widthPixels;
      std::uint32_t heightPixels;
    };
    constexpr Display DISPLAYS[] = {{1280, 720},  {1366, 768},  {1600, 900},  {1920, 1080}, {1920, 1200}, {2256, 1504},
                                    {2560, 1440}, {2560, 1600}, {2880, 1920}, {3440, 1440}, {3840, 2160}, {3840, 2400}};
    for (const Display& display : DISPLAYS)
    {
      const Neuron::PresentPass::Placement placement =
        Neuron::PresentPass::Fit(Neuron::SCREEN_WIDTH_PIXELS, Neuron::SCREEN_HEIGHT_PIXELS, display.widthPixels, display.heightPixels);
      Neuron::InputState input;
      PlaceForDisplay(input, display.widthPixels, display.heightPixels);
      const std::wstring at = L"on " + std::to_wstring(display.widthPixels) + L"x" + std::to_wstring(display.heightPixels);
      const std::int32_t middleRow = placement.topPixels + static_cast<std::int32_t>(placement.heightPixels) / 2;
      const std::int32_t middleColumn = placement.leftPixels + static_cast<std::int32_t>(placement.widthPixels) / 2;
      CheckEveryPixelOfTheAxis(input, true, display.widthPixels, placement.leftPixels, placement.widthPixels, Neuron::SCREEN_WIDTH_PIXELS,
                               middleRow, L"across " + at);
      CheckEveryPixelOfTheAxis(input, false, display.heightPixels, placement.topPixels, placement.heightPixels,
                               Neuron::SCREEN_HEIGHT_PIXELS, middleColumn, L"down " + at);
    }
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
