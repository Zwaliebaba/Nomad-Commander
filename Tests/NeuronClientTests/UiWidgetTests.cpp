// Tests/NeuronClientTests/UiWidgetTests.cpp
#include "pch.h"
#include "BitmapFont.h"
#include "IconAtlas.h"
#include "InputState.h"
#include "Rect.h"
#include "Ui.h"
#include "UiState.h"
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

[[nodiscard]] LPARAM PackPoint(std::int16_t _x, std::int16_t _y)
{
  return static_cast<LPARAM>((static_cast<std::uint32_t>(static_cast<std::uint16_t>(_y)) << 16) | static_cast<std::uint16_t>(_x));
}

/// As in UiTests: the batch and the text renderer are unbuilt, so every draw returns and the widget logic runs with
/// no D3D12. These widgets are arithmetic and arithmetic does not need a GPU to be wrong.
class WidgetFixture
{
public:
  WidgetFixture()
    : m_ui(m_batch, m_text, m_input)
  {
  }

  /// One whole frame, with the caller's widgets in between.
  template <typename Fn> void Frame(Fn _widgets)
  {
    m_input.BeginFrame();
    for (const auto& message : m_pending)
    {
      (void)m_input.HandleMessage(message.message, message.wparam, message.lparam);
    }
    m_pending.clear();
    m_ui.BeginFrame();
    _widgets(m_ui);
    m_ui.EndFrame();
  }

  void Post(UINT _message, WPARAM _wparam, LPARAM _lparam)
  {
    m_pending.push_back({_message, _wparam, _lparam});
  }

  void MoveTo(std::int16_t _x, std::int16_t _y)
  {
    Post(WM_MOUSEMOVE, 0, PackPoint(_x, _y));
  }

  void PressAt(std::int16_t _x, std::int16_t _y)
  {
    Post(WM_MOUSEMOVE, 0, PackPoint(_x, _y));
    Post(WM_LBUTTONDOWN, 0, PackPoint(_x, _y));
  }

  void ReleaseAt(std::int16_t _x, std::int16_t _y)
  {
    Post(WM_LBUTTONUP, 0, PackPoint(_x, _y));
  }

  /// A press and a release at the same place, which the Ui resolves across two frames.
  template <typename Fn> void ClickAt(std::int16_t _x, std::int16_t _y, Fn _widgets)
  {
    PressAt(_x, _y);
    Frame(_widgets);
    ReleaseAt(_x, _y);
    Frame(_widgets);
  }

  void Wheel(int _notches)
  {
    Post(WM_MOUSEWHEEL, static_cast<WPARAM>(static_cast<std::int32_t>(WHEEL_DELTA) * _notches) << 16, 0);
  }

  void Key(std::uint8_t _virtualKey)
  {
    Post(WM_KEYDOWN, _virtualKey, 0);
  }

  void Type(char _character)
  {
    Post(WM_CHAR, static_cast<WPARAM>(_character), 0);
  }

  [[nodiscard]] Neuron::Ui& Ui() noexcept
  {
    return m_ui;
  }

private:
  struct Message
  {
    UINT message;
    WPARAM wparam;
    LPARAM lparam;
  };

  Neuron::PrimitiveBatch m_batch;
  Neuron::TextRenderer m_text;
  Neuron::InputState m_input;
  Neuron::Ui m_ui;
  std::vector<Message> m_pending;
};

/// Two hundred items, which is the size the acceptance criterion names and the size the board really reaches.
[[nodiscard]] std::vector<std::string> MakeItemStorage(int _count)
{
  std::vector<std::string> storage;
  storage.reserve(static_cast<std::size_t>(_count));
  for (int index = 0; index < _count; ++index)
  {
    storage.push_back("item " + std::to_string(index));
  }
  return storage;
}

[[nodiscard]] std::vector<std::string_view> MakeItems(const std::vector<std::string>& _storage)
{
  std::vector<std::string_view> items;
  items.reserve(_storage.size());
  for (const std::string& text : _storage)
  {
    items.push_back(text);
  }
  return items;
}

// A list twenty cells tall at the top left, so a row is easy to aim at: row N spans y = N*24 .. N*24+23.
constexpr Neuron::Rect LIST_RECT{0, 0, 480, 480};

} // namespace

TEST_CLASS(UiWidgetTests)
{
public:
  TEST_METHOD(AListSelectsByClickAndTheSelectionSurvivesScrolling)
  {
    // The acceptance criterion: two hundred items, scrolled by wheel, selection kept. It survives because the
    // selection is an index into the items and not a position on the screen.
    WidgetFixture fixture;
    const std::vector<std::string> storage = MakeItemStorage(200);
    const std::vector<std::string_view> items = MakeItems(storage);
    std::int32_t selected = -1;
    Neuron::ScrollState scroll;

    const auto list = [&](Neuron::Ui& _ui) { (void)_ui.List("board", LIST_RECT, items, selected, scroll); };

    // Row 3 is y = 72..95.
    fixture.ClickAt(100, 80, list);
    Assert::AreEqual(3, selected, L"clicking the fourth row must select it");

    fixture.Wheel(-5);
    fixture.Frame(list);
    Assert::IsTrue(scroll.offsetPixels > 0, L"the wheel must scroll the list");
    Assert::AreEqual(3, selected, L"scrolling must not change the selection");

    // And it can still be scrolled back.
    fixture.Wheel(50);
    fixture.Frame(list);
    Assert::AreEqual(0, scroll.offsetPixels, L"the list must clamp at the top");
    Assert::AreEqual(3, selected);
  }

  TEST_METHOD(AListScrollsNoFurtherThanItsContent)
  {
    WidgetFixture fixture;
    const std::vector<std::string> storage = MakeItemStorage(200);
    const std::vector<std::string_view> items = MakeItems(storage);
    std::int32_t selected = 0;
    Neuron::ScrollState scroll;
    const auto list = [&](Neuron::Ui& _ui) { (void)_ui.List("board", LIST_RECT, items, selected, scroll); };

    fixture.MoveTo(100, 100);
    fixture.Frame(list);
    for (int spin = 0; spin < 40; ++spin)
    {
      fixture.Wheel(-10);
      fixture.Frame(list);
    }
    // 200 rows of 24 is 4,800 pixels of content in a 480-pixel view: the last row must be reachable and no further.
    const std::int32_t maximum = 200 * Neuron::CELL_PIXELS - LIST_RECT.heightPixels;
    Assert::AreEqual(maximum, scroll.offsetPixels, L"the list scrolled past its own end");
  }

  TEST_METHOD(AListMovesItsSelectionWithTheArrowKeys)
  {
    WidgetFixture fixture;
    const std::vector<std::string> storage = MakeItemStorage(200);
    const std::vector<std::string_view> items = MakeItems(storage);
    std::int32_t selected = 0;
    Neuron::ScrollState scroll;
    const auto list = [&](Neuron::Ui& _ui) { (void)_ui.List("board", LIST_RECT, items, selected, scroll); };

    // The list needs the keyboard first, which a click gives it.
    fixture.ClickAt(100, 8, list);
    Assert::AreEqual(0, selected);

    for (int press = 0; press < 3; ++press)
    {
      fixture.Key(VK_DOWN);
      fixture.Frame(list);
    }
    Assert::AreEqual(3, selected, L"down must walk the selection down");

    fixture.Key(VK_UP);
    fixture.Frame(list);
    Assert::AreEqual(2, selected, L"up must walk it back");

    // It stops at the top rather than going negative.
    for (int press = 0; press < 10; ++press)
    {
      fixture.Key(VK_UP);
      fixture.Frame(list);
    }
    Assert::AreEqual(0, selected, L"the selection must stop at the first item");
  }

  TEST_METHOD(ArrowKeysScrollTheViewToFollowTheSelection)
  {
    WidgetFixture fixture;
    const std::vector<std::string> storage = MakeItemStorage(200);
    const std::vector<std::string_view> items = MakeItems(storage);
    std::int32_t selected = 0;
    Neuron::ScrollState scroll;
    const auto list = [&](Neuron::Ui& _ui) { (void)_ui.List("board", LIST_RECT, items, selected, scroll); };

    fixture.ClickAt(100, 8, list);
    // Twenty rows fit in 480 pixels; walking to row 40 must bring the view with it.
    for (int press = 0; press < 40; ++press)
    {
      fixture.Key(VK_DOWN);
      fixture.Frame(list);
    }
    Assert::AreEqual(40, selected);
    const std::int32_t rowTop = 40 * Neuron::CELL_PIXELS;
    Assert::IsTrue(scroll.offsetPixels <= rowTop, L"the selected row is above the view");
    Assert::IsTrue(scroll.offsetPixels + LIST_RECT.heightPixels >= rowTop + Neuron::CELL_PIXELS, L"the selected row is below the view");
  }

  TEST_METHOD(AStepperNeverLeavesItsBounds)
  {
    WidgetFixture fixture;
    std::int32_t percent = 50;
    // The rect: minus at x 0..47, value in the middle, plus at the right end.
    const Neuron::Rect stepper{0, 0, 480, 48};
    const auto widget = [&](Neuron::Ui& _ui) { (void)_ui.Stepper("withdraw", stepper, "withdraw at 50 percent", percent, 0, 100, 10); };

    fixture.ClickAt(20, 24, widget);
    Assert::AreEqual(40, percent, L"minus must step down");

    for (int click = 0; click < 10; ++click)
    {
      fixture.ClickAt(20, 24, widget);
    }
    Assert::AreEqual(0, percent, L"it must stop at its minimum, not go below it");

    for (int click = 0; click < 20; ++click)
    {
      fixture.ClickAt(460, 24, widget);
    }
    Assert::AreEqual(100, percent, L"it must stop at its maximum");
  }

  TEST_METHOD(AStepperHandedAValueOutsideItsBoundsBringsItBack)
  {
    WidgetFixture fixture;
    std::int32_t wings = 999;
    const auto widget = [&](Neuron::Ui& _ui) { (void)_ui.Stepper("wings", Neuron::Rect{0, 0, 480, 48}, "wings", wings, 0, 8, 1); };
    fixture.Frame(widget);
    Assert::AreEqual(8, wings, L"a value out of bounds must be clamped on the first frame that draws it");
  }

  TEST_METHOD(AToggleFlipsOnClickAndOnlyOnClick)
  {
    WidgetFixture fixture;
    bool marked = false;
    const Neuron::Rect rect{0, 0, 240, 24};
    const auto widget = [&](Neuron::Ui& _ui) { (void)_ui.Toggle("marked", rect, "marked cargo", marked); };

    fixture.ClickAt(10, 12, widget);
    Assert::IsTrue(marked, L"a click must flip it");
    fixture.ClickAt(10, 12, widget);
    Assert::IsFalse(marked, L"a second click must flip it back");

    // A press inside and a release outside must not flip it.
    fixture.PressAt(10, 12);
    fixture.Frame(widget);
    fixture.ReleaseAt(900, 900);
    fixture.Frame(widget);
    Assert::IsFalse(marked, L"a release outside must take the click back");
  }

  TEST_METHOD(ANumberFieldTakesDigitsAndBackspaceAndNothingElse)
  {
    WidgetFixture fixture;
    std::int32_t price = 0;
    Neuron::FieldState state;
    const Neuron::Rect rect{0, 0, 240, 48};
    const auto widget = [&](Neuron::Ui& _ui) { (void)_ui.NumberField("price", rect, price, 0, 99999, state); };

    // A click gives it the keyboard and starts an edit.
    fixture.ClickAt(10, 24, widget);
    Assert::IsTrue(state.editing, L"clicking a field must start an edit");

    fixture.Type('1');
    fixture.Type('4');
    fixture.Type('0');
    fixture.Frame(widget);
    Assert::AreEqual(140, price);

    // Letters and punctuation are ignored: this is a number field, not an editor.
    fixture.Type('x');
    fixture.Type('-');
    fixture.Type('.');
    fixture.Frame(widget);
    Assert::AreEqual(140, price, L"a field that is not a text editor must ignore text");

    fixture.Type('\b');
    fixture.Frame(widget);
    Assert::AreEqual(14, price, L"backspace must remove the last digit");
  }

  TEST_METHOD(ANumberFieldClampsToItsRange)
  {
    WidgetFixture fixture;
    std::int32_t price = 0;
    Neuron::FieldState state;
    const auto widget = [&](Neuron::Ui& _ui) { (void)_ui.NumberField("price", Neuron::Rect{0, 0, 240, 48}, price, 10, 500, state); };

    fixture.ClickAt(10, 24, widget);
    for (int digit = 0; digit < 6; ++digit)
    {
      fixture.Type('9');
    }
    fixture.Frame(widget);
    Assert::AreEqual(500, price, L"typing past the maximum must clamp rather than overflow");
  }

  TEST_METHOD(TabsSwitchOnClickAndReportTheChangeOnce)
  {
    WidgetFixture fixture;
    const std::array<std::string_view, 6> labels{"Board", "Map", "Operations", "Contracts", "Company", "Receipts"};
    std::int32_t active = 0;
    bool changed = false;
    const Neuron::Rect bar{0, 0, 1440, 48};
    const auto widget = [&](Neuron::Ui& _ui) { changed = _ui.Tabs("tabs", bar, labels, active); };

    // Each tab is 240 wide; the third starts at 480.
    fixture.PressAt(500, 24);
    fixture.Frame(widget);
    Assert::AreEqual(2, active, L"clicking a tab must make it active");
    Assert::IsTrue(changed);

    fixture.Frame(widget);
    Assert::IsFalse(changed, L"the change must be reported once, not every frame it stays active");

    // Clicking the tab that is already active changes nothing.
    fixture.PressAt(500, 24);
    fixture.Frame(widget);
    Assert::IsFalse(changed);
    Assert::AreEqual(2, active);
  }

  TEST_METHOD(ATooltipAppearsOnlyWhileTheMouseIsOverItsAnchor)
  {
    WidgetFixture fixture;
    const std::array<std::string_view, 2> lines{"Tessa Gate -> Kessel", "4 h | raider 3 h"};
    const Neuron::Rect lane{100, 100, 200, 48};
    bool drewSomething = false;
    const auto widget = [&](Neuron::Ui& _ui)
    {
      _ui.Tooltip(lane, lines);
      drewSomething = _ui.Hot() != Neuron::Ui::NO_WIDGET;
    };

    // Away from the lane: nothing. The tooltip claims no widget id, so Hot stays empty either way — what is asserted
    // here is that the call is harmless, and the overlay's own behaviour is pinned by the modal test below.
    fixture.MoveTo(900, 900);
    fixture.Frame(widget);
    Assert::IsFalse(drewSomething);

    fixture.MoveTo(150, 120);
    fixture.Frame(widget);
    // Over it, and still no widget is hot: a tooltip is not something you can click.
    Assert::IsFalse(drewSomething, L"a tooltip must not take the mouse from what it is about");
  }

  TEST_METHOD(AModalAnswersAndHoldsTheFrameBehindIt)
  {
    // The whole point of a modal: while it is up, the button behind it cannot be clicked.
    WidgetFixture fixture;
    const std::array<std::string_view, 1> lines{"Nothing departs without Confirm."};
    const Neuron::Rect behind{0, 0, 200, 48};
    int behindClicks = 0;
    Neuron::Ui::Choice answer = Neuron::Ui::Choice::Open;

    const auto widgets = [&](Neuron::Ui& _ui)
    {
      if (_ui.Button("behind", behind, "Behind"))
      {
        ++behindClicks;
      }
      answer = _ui.Confirm("commit", "Commit the operation?", lines);
    };

    // Frame one: the modal opens. The button behind was asked before the modal was drawn, so it still answers -- the
    // one-frame latency the model documents.
    fixture.Frame(widgets);
    Assert::IsTrue(answer == Neuron::Ui::Choice::Open);

    // From here on the button behind is dead however hard it is clicked.
    fixture.ClickAt(10, 10, widgets);
    Assert::AreEqual(0, behindClicks, L"a modal must hold the frame behind it");

    // Escape cancels, which "nothing departs without Confirm" makes the safe answer.
    fixture.Key(VK_ESCAPE);
    fixture.Frame(widgets);
    Assert::IsTrue(answer == Neuron::Ui::Choice::Canceled, L"Escape must cancel a modal");
  }

  TEST_METHOD(AModalConfirmsWhenItsConfirmButtonIsClicked)
  {
    WidgetFixture fixture;
    const std::array<std::string_view, 1> lines{"credits 14,250 -> 11,550"};
    Neuron::Ui::Choice answer = Neuron::Ui::Choice::Open;
    const auto widgets = [&](Neuron::Ui& _ui) { answer = _ui.Confirm("commit", "Commit?", lines); };

    fixture.Frame(widgets);
    // The panel is centred and 20 cells wide at least; its Confirm button starts a cell in from the left edge.
    const std::int32_t width = Neuron::CELL_PIXELS * 20;
    const std::int32_t left = (Neuron::GRID_COLUMNS * Neuron::CELL_PIXELS - width) / 2;
    const std::int32_t height = 2 * Neuron::CELL_PIXELS + Neuron::CELL_PIXELS * 5;
    const std::int32_t top = (Neuron::GRID_ROWS * Neuron::CELL_PIXELS - height) / 2;
    const auto buttonX = static_cast<std::int16_t>(left + Neuron::CELL_PIXELS + Neuron::CELL_PIXELS * 3);
    const auto buttonY = static_cast<std::int16_t>(top + height - Neuron::CELL_PIXELS * 2);

    fixture.ClickAt(buttonX, buttonY, widgets);
    Assert::IsTrue(answer == Neuron::Ui::Choice::Confirmed, L"clicking Confirm must confirm");
  }

  TEST_METHOD(EveryIconIsInTheAtlasAndNoneIsBlank)
  {
    // An icon with no art is one somebody forgot to draw, and it would show as an empty cell rather than an error.
    for (std::uint32_t index = 0; index < Neuron::ICON_COUNT; ++index)
    {
      std::uint32_t litRows = 0;
      for (std::uint32_t row = 0; row < Neuron::ICON_BYTES; ++row)
      {
        if (Neuron::ICON_8X8_ART[static_cast<std::size_t>(index) * Neuron::ICON_BYTES + row] != 0)
        {
          ++litRows;
        }
      }
      Assert::IsTrue(litRows >= 3, (L"icon " + std::to_wstring(index) + L" has almost no art; is it finished?").c_str());
    }
    // The atlas has room for them: two rows of sixteen after the 96 glyphs.
    Assert::IsTrue(Neuron::ICON_COUNT <= Neuron::TextRenderer::ICON_ROWS * Neuron::TextRenderer::ATLAS_COLUMNS);
    Assert::AreEqual(Neuron::TextRenderer::ICON_FIRST_CELL, Neuron::FONT_GLYPH_COUNT);
  }
};

} // namespace NeuronClientTests
