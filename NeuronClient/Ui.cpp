// NeuronClient/Ui.cpp
#include "pch.h"
#include "Ui.h"

#include <algorithm>
#include <cstdio>
#include "Debug.h"

namespace Neuron
{

namespace
{

/// FNV-1a, 64 bits. Chosen because it is four lines, needs no table, and is stable across builds — which is the whole
/// requirement: an id must be the same number this frame and next, not hard to collide.
constexpr WidgetId FNV_OFFSET_BASIS = 1469598103934665603ULL;
constexpr WidgetId FNV_PRIME = 1099511628211ULL;

/// The ACCENT underline beneath the active tab (UI section 1: 3 px at 1920x1080).
constexpr std::int32_t TAB_UNDERLINE_PIXELS = 3;

/// A scrollbar is half a cell wide: wide enough to hit, narrow enough not to be a column of its own.
constexpr std::int32_t SCROLLBAR_PIXELS = CELL_PIXELS / 2;

/// However long the content, the thumb stays big enough to grab.
constexpr std::int32_t MINIMUM_THUMB_PIXELS = CELL_PIXELS;

/// Rows a wheel notch moves. Three is what every list in Windows does.
constexpr std::int32_t WHEEL_ROWS = 3;

/// A list row's text starts a quarter-cell in, so it does not touch the selection's edge.
constexpr std::int32_t LIST_TEXT_INSET_PIXELS = CELL_PIXELS / 4;

/// The widest a label may be before it is drawn cut rather than overflowing. A label that does not fit its rectangle
/// is a layout defect; drawing it outside would hide that, and cutting it shows it.
[[nodiscard]] std::string_view FitToWidth(std::string_view _text, std::int32_t _widthPixels, Font _font)
{
  if (_widthPixels <= 0)
  {
    return std::string_view{};
  }
  const std::size_t fits = static_cast<std::size_t>(_widthPixels) / MetricsOf(_font).advancePixels;
  return _text.size() <= fits ? _text : _text.substr(0, fits);
}

} // namespace

Ui::Ui(PrimitiveBatch& _batch, TextRenderer& _text, const InputState& _input) noexcept
  : m_batch(_batch),
    m_text(_text),
    m_input(_input)
{
}

WidgetId Ui::Hash(std::string_view _name, WidgetId _seed) noexcept
{
  WidgetId hash = _seed == 0 ? FNV_OFFSET_BASIS : _seed;
  for (const char character : _name)
  {
    hash ^= static_cast<WidgetId>(static_cast<unsigned char>(character));
    hash *= FNV_PRIME;
  }
  // Zero is NO_WIDGET, so a name that happened to hash to it would be a widget that can never be hot. One in 2^64,
  // and one line to make impossible.
  return hash == NO_WIDGET ? FNV_PRIME : hash;
}

void Ui::BeginFrame() noexcept
{
  m_hot = NO_WIDGET;
  m_pressedSomething = false;
  m_idDepth = 0;
  // Last frame's answer is this frame's rule: while a modal is up, nothing behind it is interactive.
  m_modalWasOpen = m_modalOpenThisFrame;
  m_modalOpenThisFrame = false;
  m_hint.present = false;
  m_modal.present = false;
}

void Ui::EndFrame() noexcept
{
  // A press that landed on no widget takes the keyboard away from whatever had it: clicking the background is how a
  // person says "I am done with that field".
  if (m_input.MousePressed(MouseButton::Left) && !m_pressedSomething && !m_modalOpenThisFrame)
  {
    m_focus = NO_WIDGET;
    m_active = NO_WIDGET;
  }
  // A release anywhere ends the press. The widget that was active has already had its chance to fire.
  if (m_input.MouseReleased(MouseButton::Left))
  {
    m_active = NO_WIDGET;
  }
  // The deferred drawing, last: a tooltip or a drill-in over the frame, and a modal over even that. This is what
  // makes NC-026's "Detail is never under something" true by construction rather than by call order.
  DrawOverlay(m_hint);
  DrawOverlay(m_modal);
  NOMAD_ASSERT(m_idDepth == 0);
}

void Ui::PushId(std::string_view _name) noexcept
{
  if (m_idDepth >= MAX_ID_DEPTH)
  {
    // Dropped rather than written past the end. A UI nested sixteen deep is a defect in the screen, and the assert is
    // what says so in a debug build; in a release build the ids collide, which is visible and not dangerous.
    NOMAD_ASSERT(false);
    return;
  }
  const WidgetId parent = m_idDepth == 0 ? FNV_OFFSET_BASIS : m_idStack[m_idDepth - 1];
  m_idStack[m_idDepth] = Hash(_name, parent);
  ++m_idDepth;
}

void Ui::PopId() noexcept
{
  NOMAD_ASSERT(m_idDepth > 0);
  if (m_idDepth > 0)
  {
    --m_idDepth;
  }
}

WidgetId Ui::Id(std::string_view _name) const noexcept
{
  const WidgetId parent = m_idDepth == 0 ? FNV_OFFSET_BASIS : m_idStack[m_idDepth - 1];
  return Hash(_name, parent);
}

bool Ui::UpdateHot(WidgetId _id, const Rect& _rect) noexcept
{
  if (!Interactive())
  {
    return false;
  }
  const MousePoint mouse = m_input.MousePosition();
  if (!_rect.Contains(mouse.xPixels, mouse.yPixels))
  {
    return false;
  }
  // The last claim in a frame wins, so hit order follows draw order and a panel drawn on top takes the clicks.
  m_hot = _id;
  return true;
}

Rect Ui::Panel(const Rect& _rect, std::string_view _title) noexcept
{
  if (_rect.IsEmpty())
  {
    return _rect;
  }
  // Opaque fill, one-pixel border, no shadow and no alpha (UI §1).
  m_batch.FillRect(static_cast<float>(_rect.xPixels), static_cast<float>(_rect.yPixels), static_cast<float>(_rect.widthPixels),
                   static_cast<float>(_rect.heightPixels), Palette::PANEL);
  m_batch.Rect(static_cast<float>(_rect.xPixels), static_cast<float>(_rect.yPixels), static_cast<float>(_rect.widthPixels),
               static_cast<float>(_rect.heightPixels), Palette::PANEL_EDGE);

  Rect inner = _rect.Inset(BORDER_PIXELS + PADDING_PIXELS);
  if (!_title.empty() && !inner.IsEmpty())
  {
    const Rect titleRow = inner.SplitTop(CELL_PIXELS);
    Label(titleRow, _title, Palette::TEXT, Font::Title);
    // A cell of air between the title and what follows, which is what every screen in UI §3 onward shows.
    (void)inner.SplitTop(CELL_PIXELS / 2);
  }
  return inner;
}

void Ui::Label(const Rect& _rect, std::string_view _text, std::uint32_t _colorRgba, Font _font) noexcept
{
  if (_rect.IsEmpty() || _text.empty())
  {
    return;
  }
  // Integer positions throughout: the rectangle is integers and a glyph is drawn one texel a pixel, so nothing here
  // can put a glyph at a fractional pixel (UI §1, and the reason the glyph path Loads rather than samples).
  m_text.Draw(static_cast<float>(_rect.xPixels), static_cast<float>(_rect.yPixels), FitToWidth(_text, _rect.widthPixels, _font), _colorRgba,
              _font);
}

Ui::ButtonVisual Ui::ButtonBehavior(WidgetId _id, const Rect& _rect) noexcept
{
  ButtonVisual visual;
  visual.hovered = UpdateHot(_id, _rect);
  if (visual.hovered && m_input.MousePressed(MouseButton::Left))
  {
    m_active = _id;
    m_focus = _id;
    m_pressedSomething = true;
  }
  if (m_active == _id && m_input.MouseReleased(MouseButton::Left))
  {
    // Fires only if the release is inside as well. Sliding off before letting go is how a person takes a mis-click
    // back, and it works without anybody being told it does.
    visual.clicked = visual.hovered;
  }
  visual.pressed = m_active == _id && m_input.MouseDown(MouseButton::Left);
  return visual;
}

bool Ui::Button(std::string_view _name, const Rect& _rect, std::string_view _label) noexcept
{
  if (_rect.IsEmpty())
  {
    return false;
  }
  const ButtonVisual visual = ButtonBehavior(Id(_name), _rect);
  DrawButton(_rect, _label, visual);
  return visual.clicked;
}

void Ui::DrawButton(const Rect& _rect, std::string_view _label, const ButtonVisual& _visual) noexcept
{
  const bool hovered = _visual.hovered;
  const bool pressed = _visual.pressed;
  const std::uint32_t fill = pressed ? Palette::ACCENT : hovered ? Palette::PANEL_SELECTED : Palette::PANEL;
  const std::uint32_t border = hovered || pressed ? Palette::ACCENT : Palette::PANEL_EDGE;
  // Text on an ACCENT fill is BACKGROUND (UI §2), which is the one place this widget changes the text colour.
  const std::uint32_t label = pressed ? Palette::BACKGROUND : Palette::TEXT;

  m_batch.FillRect(static_cast<float>(_rect.xPixels), static_cast<float>(_rect.yPixels), static_cast<float>(_rect.widthPixels),
                   static_cast<float>(_rect.heightPixels), fill);
  m_batch.Rect(static_cast<float>(_rect.xPixels), static_cast<float>(_rect.yPixels), static_cast<float>(_rect.widthPixels),
               static_cast<float>(_rect.heightPixels), border);

  // Centred on the cell grid: the offsets are whole pixels, so the label cannot land at a fraction.
  const TextExtent extent = TextRenderer::Measure(FitToWidth(_label, _rect.widthPixels - 2 * BORDER_PIXELS, Font::Body), Font::Body);
  const Rect labelRect{_rect.xPixels + (_rect.widthPixels - static_cast<std::int32_t>(extent.widthPixels)) / 2,
                       _rect.yPixels + (_rect.heightPixels - static_cast<std::int32_t>(extent.heightPixels)) / 2,
                       static_cast<std::int32_t>(extent.widthPixels), static_cast<std::int32_t>(extent.heightPixels)};
  Label(labelRect, _label, label);
}

// ---------------------------------------------------------------------------------------------------------------
// NC-026's widgets. Each names the line of the design that needs it.
// ---------------------------------------------------------------------------------------------------------------

bool Ui::Interactive() const noexcept
{
  // While a modal holds the frame, nothing behind it answers the mouse. It takes effect the frame AFTER the modal
  // opens, because by the time Confirm is called the frame's other widgets have already been asked -- the one-frame
  // latency every immediate-mode modal has, and invisible at sixty frames a second.
  return !m_modalWasOpen || m_inModal;
}

void Ui::Icon(const Rect& _rect, Neuron::Icon _icon, std::uint32_t _colorRgba) noexcept
{
  if (_rect.IsEmpty())
  {
    return;
  }
  m_text.DrawIcon(static_cast<float>(_rect.xPixels), static_cast<float>(_rect.yPixels), _icon, _colorRgba);
}

bool Ui::Tabs(std::string_view _name, const Rect& _rect, std::span<const std::string_view> _labels, std::int32_t& _activeIndex) noexcept
{
  if (_rect.IsEmpty() || _labels.empty())
  {
    return false;
  }
  PushId(_name);
  bool changed = false;
  const std::int32_t each = _rect.widthPixels / static_cast<std::int32_t>(_labels.size());
  Rect remaining = _rect;
  for (std::size_t index = 0; index < _labels.size(); ++index)
  {
    const bool last = index + 1 == _labels.size();
    const Rect tab = last ? remaining : remaining.SplitLeft(each);
    const bool active = static_cast<std::int32_t>(index) == _activeIndex;
    const bool hovered = UpdateHot(Id(_labels[index]), tab);

    // Active tab: PANEL fill with a three-pixel ACCENT underline. Inactive: TEXT_DIM (UI section 1).
    m_batch.FillRect(static_cast<float>(tab.xPixels), static_cast<float>(tab.yPixels), static_cast<float>(tab.widthPixels),
                     static_cast<float>(tab.heightPixels), active ? Palette::PANEL : Palette::BACKGROUND_RAISED);
    if (active)
    {
      m_batch.FillRect(static_cast<float>(tab.xPixels), static_cast<float>(tab.Bottom() - TAB_UNDERLINE_PIXELS),
                       static_cast<float>(tab.widthPixels), static_cast<float>(TAB_UNDERLINE_PIXELS), Palette::ACCENT);
    }
    const TextExtent extent = TextRenderer::Measure(_labels[index], Font::Body);
    Label(Rect{tab.xPixels + (tab.widthPixels - static_cast<std::int32_t>(extent.widthPixels)) / 2,
               tab.yPixels + (tab.heightPixels - static_cast<std::int32_t>(extent.heightPixels)) / 2,
               static_cast<std::int32_t>(extent.widthPixels), static_cast<std::int32_t>(extent.heightPixels)},
          _labels[index], active || hovered ? Palette::TEXT : Palette::TEXT_DIM);

    if (hovered && m_input.MousePressed(MouseButton::Left) && !active)
    {
      _activeIndex = static_cast<std::int32_t>(index);
      changed = true;
      m_pressedSomething = true;
    }
  }
  PopId();
  return changed;
}

void Ui::Scrollbar(std::string_view _name, const Rect& _rect, std::int32_t _contentPixels, std::int32_t _viewPixels,
                   ScrollState& _scroll) noexcept
{
  if (_rect.IsEmpty() || _viewPixels <= 0)
  {
    return;
  }
  m_batch.FillRect(static_cast<float>(_rect.xPixels), static_cast<float>(_rect.yPixels), static_cast<float>(_rect.widthPixels),
                   static_cast<float>(_rect.heightPixels), Palette::BACKGROUND_RAISED);
  if (_contentPixels <= _viewPixels)
  {
    // Nothing to scroll. The channel is still drawn, so a list does not change width when an item is removed.
    _scroll.offsetPixels = 0;
    _scroll.dragging = false;
    return;
  }

  const std::int32_t maximumOffset = _contentPixels - _viewPixels;
  const std::int32_t thumbHeight = std::max(MINIMUM_THUMB_PIXELS, _rect.heightPixels * _viewPixels / _contentPixels);
  const std::int32_t travel = _rect.heightPixels - thumbHeight;

  const WidgetId id = Id(_name);
  const bool hovered = UpdateHot(id, _rect);
  const MousePoint mouse = m_input.MousePosition();
  const std::int32_t thumbTop = _rect.yPixels + (travel <= 0 ? 0 : travel * _scroll.offsetPixels / maximumOffset);
  const Rect thumb{_rect.xPixels, thumbTop, _rect.widthPixels, thumbHeight};

  if (hovered && m_input.MousePressed(MouseButton::Left))
  {
    m_active = id;
    m_pressedSomething = true;
    _scroll.dragging = true;
    // Grabbing the thumb drags it from where it was grabbed; clicking the channel jumps it to the cursor and drags
    // from its middle. Every scrollbar does this and nobody has to be told.
    _scroll.dragGrabPixels = thumb.Contains(mouse.xPixels, mouse.yPixels) ? mouse.yPixels - thumbTop : thumbHeight / 2;
  }
  if (!m_input.MouseDown(MouseButton::Left))
  {
    _scroll.dragging = false;
  }
  if (_scroll.dragging && travel > 0)
  {
    const std::int32_t wanted = mouse.yPixels - _scroll.dragGrabPixels - _rect.yPixels;
    _scroll.offsetPixels = std::clamp(wanted * maximumOffset / travel, 0, maximumOffset);
  }
  _scroll.offsetPixels = std::clamp(_scroll.offsetPixels, 0, maximumOffset);

  const std::int32_t drawnTop = _rect.yPixels + (travel <= 0 ? 0 : travel * _scroll.offsetPixels / maximumOffset);
  m_batch.FillRect(static_cast<float>(_rect.xPixels), static_cast<float>(drawnTop), static_cast<float>(_rect.widthPixels),
                   static_cast<float>(thumbHeight), _scroll.dragging || hovered ? Palette::ACCENT : Palette::PANEL_EDGE);
}

bool Ui::List(std::string_view _name, const Rect& _rect, std::span<const std::string_view> _items, std::int32_t& _selectedIndex,
              ScrollState& _scroll) noexcept
{
  if (_rect.IsEmpty())
  {
    return false;
  }
  PushId(_name);
  bool changed = false;

  Rect body = _rect;
  const Rect barRect = body.SplitRight(SCROLLBAR_PIXELS);
  m_batch.FillRect(static_cast<float>(body.xPixels), static_cast<float>(body.yPixels), static_cast<float>(body.widthPixels),
                   static_cast<float>(body.heightPixels), Palette::PANEL);

  const std::int32_t rowHeight = CELL_PIXELS;
  const std::int32_t contentPixels = static_cast<std::int32_t>(_items.size()) * rowHeight;
  const std::int32_t maximumOffset = contentPixels > body.heightPixels ? contentPixels - body.heightPixels : 0;

  // The wheel scrolls whenever the mouse is over the list at all, which is what a person expects without aiming at
  // the bar.
  const WidgetId bodyId = Id("body");
  const bool overBody = UpdateHot(bodyId, body);
  if (overBody && m_input.WheelDelta() != 0)
  {
    _scroll.offsetPixels -= m_input.WheelDelta() * rowHeight * WHEEL_ROWS;
  }
  _scroll.offsetPixels = std::clamp(_scroll.offsetPixels, 0, maximumOffset);

  // Only the rows that can be seen are drawn or hit-tested. Two hundred items is a list the desk really has, and the
  // hundred and ninety off-screen would be work for nothing every frame.
  const std::int32_t firstVisible = _scroll.offsetPixels / rowHeight;
  const std::int32_t lastVisible = std::min(static_cast<std::int32_t>(_items.size()), firstVisible + body.heightPixels / rowHeight + 2);

  for (std::int32_t index = firstVisible; index < lastVisible; ++index)
  {
    const Rect row{body.xPixels, body.yPixels + index * rowHeight - _scroll.offsetPixels, body.widthPixels, rowHeight};
    // Clipped by hand: there is no per-widget scissor, so a row half off the end is not drawn rather than drawn over
    // whatever is next to the list.
    if (row.Bottom() <= body.yPixels || row.yPixels >= body.Bottom())
    {
      continue;
    }
    const bool wholeRowVisible = row.yPixels >= body.yPixels && row.Bottom() <= body.Bottom();
    const bool selected = index == _selectedIndex;
    const bool hovered = UpdateHot(Id(_items[static_cast<std::size_t>(index)]), row) && wholeRowVisible;
    if (selected || hovered)
    {
      m_batch.FillRect(static_cast<float>(row.xPixels), static_cast<float>(row.yPixels), static_cast<float>(row.widthPixels),
                       static_cast<float>(row.heightPixels), selected ? Palette::PANEL_SELECTED : Palette::BACKGROUND_RAISED);
    }
    Label(Rect{row.xPixels + LIST_TEXT_INSET_PIXELS, row.yPixels, row.widthPixels - LIST_TEXT_INSET_PIXELS, rowHeight},
          _items[static_cast<std::size_t>(index)], selected ? Palette::TEXT : Palette::TEXT_DIM);

    if (hovered && m_input.MousePressed(MouseButton::Left) && index != _selectedIndex)
    {
      _selectedIndex = index;
      changed = true;
      m_pressedSomething = true;
    }
  }

  if (overBody && m_input.MousePressed(MouseButton::Left))
  {
    m_focus = bodyId;
    m_pressedSomething = true;
  }
  // Up and down move the selection when the list has the keyboard, and the view follows it.
  if (m_focus == bodyId && !_items.empty() && Interactive())
  {
    std::int32_t moved = _selectedIndex;
    if (m_input.KeyPressed(VK_DOWN))
    {
      moved = std::min(static_cast<std::int32_t>(_items.size()) - 1, _selectedIndex + 1);
    }
    if (m_input.KeyPressed(VK_UP))
    {
      moved = std::max(0, _selectedIndex - 1);
    }
    if (moved != _selectedIndex)
    {
      _selectedIndex = moved;
      changed = true;
      const std::int32_t rowTop = moved * rowHeight;
      _scroll.offsetPixels = std::clamp(_scroll.offsetPixels, rowTop + rowHeight - body.heightPixels, rowTop);
      _scroll.offsetPixels = std::clamp(_scroll.offsetPixels, 0, maximumOffset);
    }
  }

  Scrollbar("bar", barRect, contentPixels, body.heightPixels, _scroll);
  m_batch.Rect(static_cast<float>(_rect.xPixels), static_cast<float>(_rect.yPixels), static_cast<float>(_rect.widthPixels),
               static_cast<float>(_rect.heightPixels), Palette::PANEL_EDGE);
  PopId();
  return changed;
}

bool Ui::Stepper(std::string_view _name, const Rect& _rect, std::string_view _label, std::int32_t& _value, std::int32_t _minimum,
                 std::int32_t _maximum, std::int32_t _step) noexcept
{
  if (_rect.IsEmpty() || _minimum > _maximum)
  {
    return false;
  }
  PushId(_name);
  Rect remaining = _rect;
  // The minus and the plus each sit in their own cell beside the value, which is what UI section 8 says the hyphen
  // means here.
  const Rect minusRect = remaining.SplitLeft(CELL_PIXELS * 2);
  const Rect plusRect = remaining.SplitRight(CELL_PIXELS * 2);

  const std::int32_t before = _value;
  if (Button("minus", minusRect, "-"))
  {
    _value -= _step;
  }
  if (Button("plus", plusRect, "+"))
  {
    _value += _step;
  }
  // It never leaves its bounds, whatever it was handed.
  _value = std::clamp(_value, _minimum, _maximum);

  // The unit is in the label and therefore on the screen (R6), which is the whole reason one is passed.
  Label(Rect{remaining.xPixels + CELL_PIXELS / 2, remaining.yPixels + (remaining.heightPixels - CELL_PIXELS) / 2, remaining.widthPixels,
             CELL_PIXELS},
        _label, Palette::TEXT);
  PopId();
  return _value != before;
}

bool Ui::Toggle(std::string_view _name, const Rect& _rect, std::string_view _label, bool& _on) noexcept
{
  if (_rect.IsEmpty())
  {
    return false;
  }
  const WidgetId id = Id(_name);
  const bool hovered = UpdateHot(id, _rect);
  bool toggled = false;
  if (hovered && m_input.MousePressed(MouseButton::Left))
  {
    m_active = id;
    m_focus = id;
    m_pressedSomething = true;
  }
  if (m_active == id && m_input.MouseReleased(MouseButton::Left) && hovered)
  {
    _on = !_on;
    toggled = true;
  }

  const Rect box{_rect.xPixels, _rect.yPixels + (_rect.heightPixels - CELL_PIXELS) / 2, CELL_PIXELS, CELL_PIXELS};
  m_batch.FillRect(static_cast<float>(box.xPixels), static_cast<float>(box.yPixels), static_cast<float>(box.widthPixels),
                   static_cast<float>(box.heightPixels), _on ? Palette::ACCENT : Palette::PANEL);
  m_batch.Rect(static_cast<float>(box.xPixels), static_cast<float>(box.yPixels), static_cast<float>(box.widthPixels),
               static_cast<float>(box.heightPixels), hovered ? Palette::ACCENT : Palette::PANEL_EDGE);
  if (_on)
  {
    Icon(box, Neuron::Icon::Selected, Palette::BACKGROUND);
  }
  Label(Rect{box.Right() + CELL_PIXELS / 2, box.yPixels, _rect.widthPixels - CELL_PIXELS, CELL_PIXELS}, _label, Palette::TEXT);
  return toggled;
}

bool Ui::NumberField(std::string_view _name, const Rect& _rect, std::int32_t& _value, std::int32_t _minimum, std::int32_t _maximum,
                     FieldState& _state) noexcept
{
  if (_rect.IsEmpty())
  {
    return false;
  }
  const WidgetId id = Id(_name);
  const bool hovered = UpdateHot(id, _rect);
  if (hovered && m_input.MousePressed(MouseButton::Left))
  {
    m_focus = id;
    m_pressedSomething = true;
    // An edit starts from what is there, so clicking into a field does not wipe it.
    char rendered[FieldState::MAX_DIGITS + 2] = {};
    sprintf_s(rendered, "%d", std::max(0, _value));
    _state.length = 0;
    for (std::size_t index = 0; rendered[index] != '\0' && _state.length < FieldState::MAX_DIGITS; ++index)
    {
      _state.digits[_state.length++] = rendered[index];
    }
    _state.digits[_state.length] = '\0';
    _state.editing = true;
  }

  const bool focused = m_focus == id;
  bool changed = false;
  if (focused && _state.editing && Interactive())
  {
    // Digits and backspace, and nothing else: this is a number field, not an editor, and nothing else in the desk
    // types at all.
    for (const char16_t unit : m_input.TypedCharacters())
    {
      if (unit >= u'0' && unit <= u'9' && _state.length < FieldState::MAX_DIGITS)
      {
        _state.digits[_state.length++] = static_cast<char>(unit);
        _state.digits[_state.length] = '\0';
      }
      else if (unit == u'\b' && _state.length > 0)
      {
        --_state.length;
        _state.digits[_state.length] = '\0';
      }
    }
    std::int64_t parsed = 0;
    for (std::uint8_t index = 0; index < _state.length; ++index)
    {
      parsed = parsed * 10 + (_state.digits[index] - '0');
      if (parsed > _maximum)
      {
        parsed = _maximum;
        break;
      }
    }
    const std::int32_t clamped = std::clamp(static_cast<std::int32_t>(parsed), _minimum, _maximum);
    if (clamped != _value)
    {
      _value = clamped;
      changed = true;
    }
  }
  if (!focused)
  {
    _state.editing = false;
  }

  m_batch.FillRect(static_cast<float>(_rect.xPixels), static_cast<float>(_rect.yPixels), static_cast<float>(_rect.widthPixels),
                   static_cast<float>(_rect.heightPixels), Palette::PANEL);
  m_batch.Rect(static_cast<float>(_rect.xPixels), static_cast<float>(_rect.yPixels), static_cast<float>(_rect.widthPixels),
               static_cast<float>(_rect.heightPixels), focused ? Palette::ACCENT : Palette::PANEL_EDGE);
  char shown[FieldState::MAX_DIGITS + 2] = {};
  if (focused && _state.editing)
  {
    std::size_t at = 0;
    for (; at < _state.length; ++at)
    {
      shown[at] = _state.digits[at];
    }
    shown[at] = '_';
  }
  else
  {
    sprintf_s(shown, "%d", _value);
  }
  Label(Rect{_rect.xPixels + CELL_PIXELS / 4, _rect.yPixels + (_rect.heightPixels - CELL_PIXELS) / 2, _rect.widthPixels, CELL_PIXELS},
        shown, Palette::TEXT);
  return changed;
}

void Ui::FillOverlay(Overlay& _overlay, const Rect& _anchorRect, std::span<const std::string_view> _lines,
                     std::uint32_t _borderRgba) noexcept
{
  _overlay.lineCount = 0;
  _overlay.hasButtons = false;
  std::int32_t widest = 0;
  for (const std::string_view line : _lines)
  {
    if (_overlay.lineCount == Overlay::MAX_LINES)
    {
      break;
    }
    const std::size_t taken = line.size() < Overlay::MAX_LINE_CHARS ? line.size() : Overlay::MAX_LINE_CHARS;
    for (std::size_t index = 0; index < taken; ++index)
    {
      _overlay.lines[_overlay.lineCount][index] = line[index];
    }
    _overlay.lines[_overlay.lineCount][taken] = '\0';
    widest = std::max(widest, static_cast<std::int32_t>(taken));
    ++_overlay.lineCount;
  }
  if (_overlay.lineCount == 0)
  {
    return;
  }

  const auto glyphWidth = static_cast<std::int32_t>(MetricsOf(Font::Body).advancePixels);
  const std::int32_t width = widest * glyphWidth + 2 * CELL_PIXELS;
  const std::int32_t height = _overlay.lineCount * CELL_PIXELS + 2 * CELL_PIXELS;
  // Placed below and right of what it is about, then pulled back inside the screen rather than allowed off it.
  const std::int32_t left = std::min(_anchorRect.xPixels, GRID_COLUMNS * CELL_PIXELS - width);
  const std::int32_t top = std::min(_anchorRect.Bottom() + CELL_PIXELS / 2, GRID_ROWS * CELL_PIXELS - height);
  _overlay.rect = Rect{std::max(0, left), std::max(0, top), width, height};
  _overlay.borderRgba = _borderRgba;
  _overlay.present = true;
}

void Ui::DrawOverlay(const Overlay& _overlay) noexcept
{
  if (!_overlay.present || _overlay.lineCount == 0)
  {
    return;
  }
  m_batch.FillRect(static_cast<float>(_overlay.rect.xPixels), static_cast<float>(_overlay.rect.yPixels),
                   static_cast<float>(_overlay.rect.widthPixels), static_cast<float>(_overlay.rect.heightPixels),
                   Palette::BACKGROUND_RAISED);
  m_batch.Rect(static_cast<float>(_overlay.rect.xPixels), static_cast<float>(_overlay.rect.yPixels),
               static_cast<float>(_overlay.rect.widthPixels), static_cast<float>(_overlay.rect.heightPixels), _overlay.borderRgba);
  Rect line = _overlay.rect.Inset(CELL_PIXELS);
  for (std::uint8_t index = 0; index < _overlay.lineCount; ++index)
  {
    const Rect row = line.SplitTop(CELL_PIXELS);
    Label(row, _overlay.lines[index], index == 0 ? Palette::TEXT : Palette::TEXT_DIM);
  }
  if (_overlay.hasButtons)
  {
    DrawButton(_overlay.confirmRect, "Confirm", _overlay.confirmVisual);
    DrawButton(_overlay.cancelRect, "Cancel", _overlay.cancelVisual);
  }
}

void Ui::Tooltip(const Rect& _anchorRect, std::span<const std::string_view> _lines) noexcept
{
  const MousePoint mouse = m_input.MousePosition();
  if (!Interactive() || !_anchorRect.Contains(mouse.xPixels, mouse.yPixels))
  {
    return;
  }
  FillOverlay(m_hint, _anchorRect, _lines, Palette::PANEL_EDGE);
}

void Ui::Detail(const Rect& _anchorRect, std::span<const std::string_view> _lines) noexcept
{
  FillOverlay(m_hint, _anchorRect, _lines, Palette::ACCENT);
}

Ui::Choice Ui::Confirm(std::string_view _name, std::string_view _title, std::span<const std::string_view> _lines) noexcept
{
  m_modalOpenThisFrame = true;
  m_inModal = true;
  PushId(_name);

  std::int32_t widest = static_cast<std::int32_t>(_title.size());
  for (const std::string_view line : _lines)
  {
    widest = std::max(widest, static_cast<std::int32_t>(line.size()));
  }
  const auto glyphWidth = static_cast<std::int32_t>(MetricsOf(Font::Body).advancePixels);
  const std::int32_t width = std::max(CELL_PIXELS * 20, widest * glyphWidth + 2 * CELL_PIXELS);
  const std::int32_t height = (static_cast<std::int32_t>(_lines.size()) + 1) * CELL_PIXELS + CELL_PIXELS * 5;
  const Rect panel{(GRID_COLUMNS * CELL_PIXELS - width) / 2, (GRID_ROWS * CELL_PIXELS - height) / 2, width, height};

  // The buttons are resolved now and drawn at EndFrame over the panel, because the modal has to be above everything
  // and the frame behind it has already been drawn.
  Rect body = panel.Inset(CELL_PIXELS);
  Rect buttons = body.SplitBottom(CELL_PIXELS * 2);
  const Rect confirmRect = buttons.SplitLeft(CELL_PIXELS * 7);
  const Rect cancelRect = buttons.SplitRight(CELL_PIXELS * 7);

  const ButtonVisual confirmVisual = ButtonBehavior(Id("confirm"), confirmRect);
  const ButtonVisual cancelVisual = ButtonBehavior(Id("cancel"), cancelRect);
  Choice choice = Choice::Open;
  if (confirmVisual.clicked)
  {
    choice = Choice::Confirmed;
  }
  if (cancelVisual.clicked)
  {
    choice = Choice::Canceled;
  }
  // Escape is Cancel, which "nothing departs without Confirm" (UI section 6) makes the safe answer.
  if (m_input.KeyPressed(VK_ESCAPE))
  {
    choice = Choice::Canceled;
  }

  m_modal.lineCount = 0;
  const std::size_t titleLength = _title.size() < Overlay::MAX_LINE_CHARS ? _title.size() : Overlay::MAX_LINE_CHARS;
  for (std::size_t index = 0; index < titleLength; ++index)
  {
    m_modal.lines[0][index] = _title[index];
  }
  m_modal.lines[0][titleLength] = '\0';
  m_modal.lineCount = 1;
  for (const std::string_view line : _lines)
  {
    if (m_modal.lineCount == Overlay::MAX_LINES)
    {
      break;
    }
    const std::size_t taken = line.size() < Overlay::MAX_LINE_CHARS ? line.size() : Overlay::MAX_LINE_CHARS;
    for (std::size_t index = 0; index < taken; ++index)
    {
      m_modal.lines[m_modal.lineCount][index] = line[index];
    }
    m_modal.lines[m_modal.lineCount][taken] = '\0';
    ++m_modal.lineCount;
  }
  m_modal.rect = panel;
  m_modal.borderRgba = Palette::ACCENT;
  m_modal.confirmRect = confirmRect;
  m_modal.cancelRect = cancelRect;
  m_modal.confirmVisual = confirmVisual;
  m_modal.cancelVisual = cancelVisual;
  m_modal.hasButtons = true;
  m_modal.present = true;

  PopId();
  m_inModal = false;
  if (choice != Choice::Open)
  {
    // Answered: it closes this frame rather than lingering for one more.
    m_modalOpenThisFrame = false;
    m_modal.present = false;
  }
  return choice;
}
} // namespace Neuron
