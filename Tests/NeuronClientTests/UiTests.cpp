// Tests/NeuronClientTests/UiTests.cpp
#include "pch.h"
#include "InputState.h"
#include "Palette.h"
#include "Rect.h"
#include "Ui.h"
#include <cstdint>
#include <string>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

[[nodiscard]] LPARAM PackPoint(std::int16_t _x, std::int16_t _y)
{
  return static_cast<LPARAM>((static_cast<std::uint32_t>(static_cast<std::uint16_t>(_y)) << 16) | static_cast<std::uint16_t>(_x));
}

/// A Ui over batches that were never given a device.
///
/// `PrimitiveBatch` and `TextRenderer` are unbuilt here on purpose: every draw call they receive checks its mapped
/// pointer and returns, so the widget logic runs with no D3D12 at all. That is what makes these tests the fast ones —
/// hit testing and the click protocol are arithmetic, and arithmetic does not need a GPU to be wrong.
class UiFixture
{
public:
  UiFixture()
    : m_ui(m_batch, m_text, m_input)
  {
  }

  /// One frame: roll the input edges, feed the messages, run the widgets.
  void BeginFrame() noexcept
  {
    m_input.BeginFrame();
  }

  void Send(UINT _message, WPARAM _wparam, LPARAM _lparam) noexcept
  {
    (void)m_input.HandleMessage(_message, _wparam, _lparam);
  }

  void MoveMouse(std::int16_t _x, std::int16_t _y) noexcept
  {
    Send(WM_MOUSEMOVE, 0, PackPoint(_x, _y));
  }

  void PressLeft(std::int16_t _x, std::int16_t _y) noexcept
  {
    Send(WM_LBUTTONDOWN, 0, PackPoint(_x, _y));
  }

  void ReleaseLeft(std::int16_t _x, std::int16_t _y) noexcept
  {
    Send(WM_LBUTTONUP, 0, PackPoint(_x, _y));
  }

  [[nodiscard]] Neuron::Ui& Ui() noexcept
  {
    return m_ui;
  }

private:
  Neuron::PrimitiveBatch m_batch;
  Neuron::TextRenderer m_text;
  Neuron::InputState m_input;
  Neuron::Ui m_ui;
};

constexpr Neuron::Rect BUTTON{100, 100, 200, 48};

} // namespace

TEST_CLASS(RectTests)
{
public:
  TEST_METHOD(ContainsIsHalfOpenSoTouchingRectanglesDoNotOverlap)
  {
    // The same convention FillRect fills, so what a hit test claims and what the screen shows are the same pixels.
    const Neuron::Rect rect{10, 20, 30, 40};
    Assert::IsTrue(rect.Contains(10, 20), L"the top-left corner is inside");
    Assert::IsTrue(rect.Contains(39, 59), L"the last pixel is inside");
    Assert::IsFalse(rect.Contains(40, 59), L"one past the right edge is outside");
    Assert::IsFalse(rect.Contains(39, 60), L"one past the bottom edge is outside");
    Assert::IsFalse(rect.Contains(9, 20));
  }

  TEST_METHOD(SplitTakesFromTheRectangleAndLeavesTheRest)
  {
    Neuron::Rect body{0, 0, 400, 300};
    const Neuron::Rect header = body.SplitTop(48);
    Assert::AreEqual(48, header.heightPixels);
    Assert::AreEqual(0, header.yPixels);
    Assert::AreEqual(48, body.yPixels, L"the remainder starts below what was taken");
    Assert::AreEqual(252, body.heightPixels);

    const Neuron::Rect sidebar = body.SplitRight(100);
    Assert::AreEqual(300, sidebar.xPixels);
    Assert::AreEqual(100, sidebar.widthPixels);
    Assert::AreEqual(300, body.widthPixels);
    // The two pieces tile the original exactly: no gap, no overlap.
    Assert::AreEqual(body.Right(), sidebar.xPixels);
  }

  TEST_METHOD(SplittingMoreThanThereIsTakesWhatThereIs)
  {
    // Not named small: rpcndr.h, which <windows.h> brings in, defines that as char.
    Neuron::Rect tiny{0, 0, 10, 10};
    const Neuron::Rect taken = tiny.SplitLeft(50);
    Assert::AreEqual(10, taken.widthPixels, L"it cannot take more than exists");
    Assert::AreEqual(0, tiny.widthPixels);
    Assert::IsTrue(tiny.IsEmpty());
  }

  TEST_METHOD(CellIsTheTwentyFourPixelGrid)
  {
    // UI §1: the 80x45 grid on the 1920x1080 screen.
    Assert::AreEqual(24, Neuron::CELL_PIXELS);
    Assert::AreEqual(1920, Neuron::GRID_COLUMNS * Neuron::CELL_PIXELS);
    Assert::AreEqual(1080, Neuron::GRID_ROWS * Neuron::CELL_PIXELS);

    const Neuron::Rect cell = Neuron::Rect::Cell(2, 1, 6, 2);
    Assert::AreEqual(48, cell.xPixels);
    Assert::AreEqual(24, cell.yPixels);
    Assert::AreEqual(144, cell.widthPixels);
    Assert::AreEqual(48, cell.heightPixels);
  }

  TEST_METHOD(InsetShrinksOnEverySide)
  {
    const Neuron::Rect padded = Neuron::Rect{0, 0, 100, 100}.Inset(24);
    Assert::AreEqual(24, padded.xPixels);
    Assert::AreEqual(52, padded.widthPixels);
  }
};

TEST_CLASS(UiTests)
{
public:
  TEST_METHOD(AButtonFiresOnPressAndReleaseInsideIt)
  {
    UiFixture fixture;

    // Frame 1: press inside. Nothing fires yet — a press is not a click.
    fixture.BeginFrame();
    fixture.MoveMouse(150, 120);
    fixture.PressLeft(150, 120);
    fixture.Ui().BeginFrame();
    Assert::IsFalse(fixture.Ui().Button("go", BUTTON, "Confirm"), L"a press alone must not fire");
    Assert::AreNotEqual(Neuron::Ui::NO_WIDGET, fixture.Ui().Active(), L"the press should have made it active");
    fixture.Ui().EndFrame();

    // Frame 2: release inside. This is the click.
    fixture.BeginFrame();
    fixture.ReleaseLeft(150, 120);
    fixture.Ui().BeginFrame();
    Assert::IsTrue(fixture.Ui().Button("go", BUTTON, "Confirm"), L"press then release inside is a click");
    fixture.Ui().EndFrame();

    // Frame 3: nothing. It must not fire twice.
    fixture.BeginFrame();
    fixture.Ui().BeginFrame();
    Assert::IsFalse(fixture.Ui().Button("go", BUTTON, "Confirm"), L"a click must fire exactly once");
    fixture.Ui().EndFrame();
  }

  TEST_METHOD(SlidingOffBeforeReleasingTakesTheClickBack)
  {
    // The property that makes a mis-click recoverable, which people expect without knowing they expect it.
    UiFixture fixture;

    fixture.BeginFrame();
    fixture.MoveMouse(150, 120);
    fixture.PressLeft(150, 120);
    fixture.Ui().BeginFrame();
    Assert::IsFalse(fixture.Ui().Button("go", BUTTON, "Confirm"));
    fixture.Ui().EndFrame();

    fixture.BeginFrame();
    fixture.MoveMouse(600, 600);
    fixture.ReleaseLeft(600, 600);
    fixture.Ui().BeginFrame();
    Assert::IsFalse(fixture.Ui().Button("go", BUTTON, "Confirm"), L"a release outside must not fire");
    fixture.Ui().EndFrame();
    Assert::AreEqual(Neuron::Ui::NO_WIDGET, fixture.Ui().Active(), L"the release should have ended the press");
  }

  TEST_METHOD(APressOutsideAButtonNeverReachesIt)
  {
    UiFixture fixture;
    fixture.BeginFrame();
    fixture.MoveMouse(10, 10);
    fixture.PressLeft(10, 10);
    fixture.Ui().BeginFrame();
    Assert::IsFalse(fixture.Ui().Button("go", BUTTON, "Confirm"));
    Assert::AreEqual(Neuron::Ui::NO_WIDGET, fixture.Ui().Hot(), L"nothing is under a mouse outside every widget");
    fixture.Ui().EndFrame();

    fixture.BeginFrame();
    fixture.ReleaseLeft(150, 120);
    fixture.Ui().BeginFrame();
    Assert::IsFalse(fixture.Ui().Button("go", BUTTON, "Confirm"), L"a release inside without a press inside must not fire");
    fixture.Ui().EndFrame();
  }

  TEST_METHOD(HoverIsTheWidgetUnderTheMouse)
  {
    UiFixture fixture;
    fixture.BeginFrame();
    fixture.MoveMouse(150, 120);
    fixture.Ui().BeginFrame();
    const Neuron::WidgetId expected = fixture.Ui().Id("go");
    (void)fixture.Ui().Button("go", BUTTON, "Confirm");
    Assert::AreEqual(expected, fixture.Ui().Hot());
    fixture.Ui().EndFrame();

    fixture.BeginFrame();
    fixture.MoveMouse(900, 900);
    fixture.Ui().BeginFrame();
    (void)fixture.Ui().Button("go", BUTTON, "Confirm");
    Assert::AreEqual(Neuron::Ui::NO_WIDGET, fixture.Ui().Hot(), L"hover must not survive the mouse leaving");
    fixture.Ui().EndFrame();
  }

  TEST_METHOD(TheLastWidgetToClaimTheMouseWins)
  {
    // Hit order follows draw order, so a panel drawn over another takes its clicks. Two overlapping buttons: the
    // second one drawn is the one that is hot.
    UiFixture fixture;
    fixture.BeginFrame();
    fixture.MoveMouse(150, 120);
    fixture.Ui().BeginFrame();
    (void)fixture.Ui().Button("under", BUTTON, "Under");
    const Neuron::WidgetId over = fixture.Ui().Id("over");
    (void)fixture.Ui().Button("over", BUTTON, "Over");
    Assert::AreEqual(over, fixture.Ui().Hot(), L"the last drawn must take the mouse");
    fixture.Ui().EndFrame();
  }

  TEST_METHOD(AnIdIsStableAcrossFramesAndUniquePerCallSite)
  {
    UiFixture fixture;
    fixture.Ui().BeginFrame();
    const Neuron::WidgetId first = fixture.Ui().Id("confirm");
    fixture.Ui().EndFrame();

    fixture.Ui().BeginFrame();
    Assert::AreEqual(first, fixture.Ui().Id("confirm"), L"the same name must be the same widget next frame");
    Assert::AreNotEqual(first, fixture.Ui().Id("cancel"), L"two names must be two widgets");
    Assert::AreNotEqual(Neuron::Ui::NO_WIDGET, first, L"no id may be NO_WIDGET");
    fixture.Ui().EndFrame();
  }

  TEST_METHOD(PushIdMakesTheSameNameADifferentWidget)
  {
    // Two panels each with a "Confirm" button is the case this exists for: without the parent id they would be one
    // widget, and clicking either would press both.
    UiFixture fixture;
    fixture.Ui().BeginFrame();
    const Neuron::WidgetId bare = fixture.Ui().Id("confirm");

    fixture.Ui().PushId("panel/offer");
    const Neuron::WidgetId inOffer = fixture.Ui().Id("confirm");
    fixture.Ui().PopId();

    fixture.Ui().PushId("panel/accusation");
    const Neuron::WidgetId inAccusation = fixture.Ui().Id("confirm");
    fixture.Ui().PopId();

    Assert::AreNotEqual(bare, inOffer);
    Assert::AreNotEqual(bare, inAccusation);
    Assert::AreNotEqual(inOffer, inAccusation, L"the same name under two parents must be two widgets");

    // And pushing the same parent again gives the same id back.
    fixture.Ui().PushId("panel/offer");
    Assert::AreEqual(inOffer, fixture.Ui().Id("confirm"));
    fixture.Ui().PopId();
    fixture.Ui().EndFrame();
  }

  TEST_METHOD(ClickingABackgroundClearsTheFocus)
  {
    UiFixture fixture;
    fixture.BeginFrame();
    fixture.MoveMouse(150, 120);
    fixture.PressLeft(150, 120);
    fixture.Ui().BeginFrame();
    (void)fixture.Ui().Button("go", BUTTON, "Confirm");
    fixture.Ui().EndFrame();
    Assert::AreEqual(fixture.Ui().Id("go"), fixture.Ui().Focus(), L"pressing a button gives it the keyboard");

    fixture.BeginFrame();
    fixture.MoveMouse(900, 900);
    fixture.PressLeft(900, 900);
    fixture.Ui().BeginFrame();
    (void)fixture.Ui().Button("go", BUTTON, "Confirm");
    fixture.Ui().EndFrame();
    Assert::AreEqual(Neuron::Ui::NO_WIDGET, fixture.Ui().Focus(),
                     L"clicking the background is how a person says they are done with a field");
  }

  TEST_METHOD(APanelReturnsTheRectangleInsideItsPaddingAndBorder)
  {
    UiFixture fixture;
    fixture.Ui().BeginFrame();
    const Neuron::Rect outer{0, 0, 480, 240};
    const Neuron::Rect inner = fixture.Ui().Panel(outer, "REPORTS");
    fixture.Ui().EndFrame();

    // One pixel of border and one cell of padding on every side (UI §1), then the title row and half a cell of air.
    Assert::AreEqual(1 + Neuron::CELL_PIXELS, inner.xPixels);
    Assert::AreEqual(480 - 2 * (1 + Neuron::CELL_PIXELS), inner.widthPixels);
    Assert::IsTrue(inner.yPixels > 1 + Neuron::CELL_PIXELS, L"the title must take a row off the top");
    Assert::IsTrue(inner.heightPixels > 0);
  }

  TEST_METHOD(ThePaletteIsTheUiSpecsAndTheBytesAreWhatWasAuthored)
  {
    // R12: the target is _UNORM, so a colour authored 0xE6 is 0xE6 on the glass. This pins the packing as well as the
    // values — red in the low byte, which is what PrimitiveBatch and the readback tests both assume.
    Assert::AreEqual(0xFFD6E1E6u, Neuron::Palette::TEXT, L"TEXT must be #E6E1D6 packed with red low");
    Assert::AreEqual(0xFF41A4D9u, Neuron::Palette::ACCENT, L"ACCENT must be #D9A441");
    Assert::AreEqual(0xFF130E0Bu, Neuron::Palette::BACKGROUND, L"BACKGROUND must be #0B0E13");
    Assert::AreEqual(0xFF4A52C9u, Neuron::Palette::EMPIRE_0_VARN, L"Varn must be #C9524A");
  }
};

} // namespace NeuronClientTests
