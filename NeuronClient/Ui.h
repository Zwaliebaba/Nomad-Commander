// NeuronClient/Ui.h
#pragma once

#include "IconAtlas.h"
#include "InputState.h"
#include "NeuronCore.h"
#include "UiState.h"
#include "Palette.h"
#include "PrimitiveBatch.h"
#include "Rect.h"
#include "TextRenderer.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace Neuron
{

/// What identifies a widget between frames. Hashed from a caller-supplied name and the ids pushed above it, so the
/// same call site is the same widget every frame and two buttons labelled "Confirm" in different panels are not.
using WidgetId = std::uint64_t;

/// The immediate-mode widget layer every screen in Phase 5 is built from (ADR-012).
///
/// **Immediate mode means there is no widget tree.** A screen is a function that runs every frame and calls `Button`
/// where a button goes; the button returns whether it was clicked. Nothing is created, nothing is destroyed, nothing
/// is kept in sync with the model — which is the point, because the desk's panels are a function of the reports and
/// beliefs the client was handed, and a retained tree would be a second copy of that to keep honest.
///
/// What `Ui` keeps between frames is three ids and a scalar: which widget the mouse is over, which one a press
/// started in, which one has the keyboard, and where the caret is. Everything else is recomputed.
///
/// **It knows nothing about the game** (R9): no fleet, no report, no empire. It draws rectangles and text and answers
/// questions about the mouse. `namespace Nomad` is where a courier lives.
class Ui
{
public:
  /// Nothing is over the mouse, nothing is pressed, nothing has focus.
  static constexpr WidgetId NO_WIDGET = 0;

  /// How deep `PushId` may go. A screen nests a panel inside a tab inside a screen; sixteen is far past that, and a
  /// fixed depth keeps `PushId` from allocating on a path the whole UI runs through.
  static constexpr std::size_t MAX_ID_DEPTH = 16;

  /// A panel's inner padding and a button's text inset, in pixels: one cell (UI §1).
  static constexpr std::int32_t PADDING_PIXELS = CELL_PIXELS;

  /// Panels and buttons carry a one-pixel border (UI §1: "1 px PANEL_EDGE border... no shadow, no alpha, no rounded
  /// corners").
  static constexpr std::int32_t BORDER_PIXELS = 1;

  Ui(PrimitiveBatch& _batch, TextRenderer& _text, const InputState& _input) noexcept;
  Ui(const Ui&) = delete;
  Ui& operator=(const Ui&) = delete;
  Ui(Ui&&) = delete;
  Ui& operator=(Ui&&) = delete;
  ~Ui() = default;

  /// Clears the frame's hover. Called after the batch and the text renderer have begun, before any widget.
  void BeginFrame() noexcept;

  /// Settles the frame: a press that landed on nothing clears the focus, and a release with no widget under it drops
  /// whatever was active, so a drag released over the background does not leave a button stuck down.
  void EndFrame() noexcept;

  /// Pushes a name onto the id stack. Everything built while it is pushed is identified relative to it.
  void PushId(std::string_view _name) noexcept;
  void PopId() noexcept;

  /// The id a widget with this name would have here. Public because a caller sometimes needs the id before the
  /// widget — to ask whether it has focus, say.
  [[nodiscard]] WidgetId Id(std::string_view _name) const noexcept;

  /// An opaque panel: PANEL fill, one-pixel PANEL_EDGE border, and a title in TEXT along its top inside the padding.
  /// Returns the rectangle inside the border and padding, which is where the caller puts its contents.
  Rect Panel(const Rect& _rect, std::string_view _title) noexcept;

  /// Text at the rectangle's top left, at GLYPH_SCALE, clipped to nothing — the caller sizes the rectangle with
  /// `TextRenderer::Measure` if it needs to.
  void Label(const Rect& _rect, std::string_view _text, std::uint32_t _colorRgba) noexcept;

  /// A button. True on the frame it is clicked, which is a press and a release both inside it.
  ///
  /// The protocol is the one every immediate-mode UI uses and it is worth stating: a press inside makes the button
  /// *active*; while active it draws pressed; a release inside fires; a release anywhere else does not. That is what
  /// makes a mis-click recoverable by sliding off the button before letting go, which people expect without knowing
  /// they expect it.
  [[nodiscard]] bool Button(std::string_view _name, const Rect& _rect, std::string_view _label) noexcept;

  // ---- NC-026's widgets. Each names the line of the design that needs it; one with no line is one the desk does
  // ---- not need (NC-026's note).

  /// One icon, tinted, on the cell grid. It accompanies a label and never replaces one: the desk is read, not
  /// scanned (UI §4).
  void Icon(const Rect& _rect, Neuron::Icon _icon, std::uint32_t _colorRgba) noexcept;

  /// The desk's screens: `Board | Map | Operations | Contracts | Company | Receipts` (UI §1). True when the active
  /// tab changed this frame.
  [[nodiscard]] bool Tabs(std::string_view _name, const Rect& _rect, std::span<const std::string_view> _labels,
                          std::int32_t& _activeIndex) noexcept;

  /// A selectable, scrolling list: the board's items, the offers, the hypothesis readings (GDD §3, §4; UI §3, §5).
  /// True when the selection changed. Wheel and bar both scroll, and the selection survives scrolling because it is
  /// an index into the items rather than a position on the screen.
  [[nodiscard]] bool List(std::string_view _name, const Rect& _rect, std::span<const std::string_view> _items, std::int32_t& _selectedIndex,
                          ScrollState& _scroll) noexcept;

  /// The bar on its own, for a caller scrolling something that is not a list. Content and view are in pixels.
  void Scrollbar(std::string_view _name, const Rect& _rect, std::int32_t _contentPixels, std::int32_t _viewPixels,
                 ScrollState& _scroll) noexcept;

  /// A bounded number with a unit in its label: withdrawal percent, fuel to buy, wing counts (GDD §4, §12; UI §6).
  /// True when the value changed. It never leaves its bounds.
  [[nodiscard]] bool Stepper(std::string_view _name, const Rect& _rect, std::string_view _label, std::int32_t& _value,
                             std::int32_t _minimum, std::int32_t _maximum, std::int32_t _step) noexcept;

  /// A two-state switch: marked or unmarked cargo, a governor's hold-or-evacuate (GDD §5, §11). True when toggled.
  [[nodiscard]] bool Toggle(std::string_view _name, const Rect& _rect, std::string_view _label, bool& _on) noexcept;

  /// Digits and backspace only, parsed to an integer: a price for a sell rule (GDD §10). Nothing else in the desk
  /// types text, which is why this is a number field and not an editor. True when the value changed.
  [[nodiscard]] bool NumberField(std::string_view _name, const Rect& _rect, std::int32_t& _value, std::int32_t _minimum,
                                 std::int32_t _maximum, FieldState& _state) noexcept;

  /// Shown while the mouse rests over a rectangle: lane hours on the map, a source's track record (UI §5). Deferred
  /// to the end of the frame so it is never under anything.
  void Tooltip(const Rect& _anchorRect, std::span<const std::string_view> _lines) noexcept;

  /// The "one tap behind" drill-in (GDD §13): the report, its source and its age, one click from the decision. An
  /// opaque panel, deferred like a tooltip, so it is drawn last and covers what it is about.
  void Detail(const Rect& _anchorRect, std::span<const std::string_view> _lines) noexcept;

  /// What a modal confirmation answered.
  enum class Choice : std::uint8_t
  {
    Open,      ///< still asking
    Confirmed, ///< commit the operation, accept the contract
    Canceled   ///< the player backed out; nothing departs without Confirm (UI §6)
  };

  /// A modal question, centred, opaque, drawn over everything. While one is open nothing behind it can be hovered or
  /// clicked — which takes effect the frame after it opens, because a frame's widgets have already been asked by the
  /// time the modal is drawn.
  [[nodiscard]] Choice Confirm(std::string_view _name, std::string_view _title, std::span<const std::string_view> _lines) noexcept;

  /// True while a modal is holding the frame, so a caller can skip work behind it.
  [[nodiscard]] bool ModalOpen() const noexcept
  {
    return m_modalWasOpen;
  }

  /// The widget the mouse is over, the one a press started in, and the one with the keyboard.
  [[nodiscard]] WidgetId Hot() const noexcept
  {
    return m_hot;
  }

  [[nodiscard]] WidgetId Active() const noexcept
  {
    return m_active;
  }

  [[nodiscard]] WidgetId Focus() const noexcept
  {
    return m_focus;
  }

  void SetFocus(WidgetId _id) noexcept
  {
    m_focus = _id;
  }

private:
  /// Marks a widget as under the mouse if it is, and returns whether it is. The last widget to claim the mouse in a
  /// frame wins, which is what makes draw order the same as hit order: a panel drawn over another takes its clicks.
  [[nodiscard]] bool UpdateHot(WidgetId _id, const Rect& _rect) noexcept;

  /// The FNV-1a hash the ids are built from, folded with the id above it.
  [[nodiscard]] static WidgetId Hash(std::string_view _name, WidgetId _seed) noexcept;

  /// What a button looks like this frame, separated from drawing it so a modal can resolve its buttons during the
  /// frame and draw them at the end, over the panel they sit on.
  struct ButtonVisual
  {
    bool clicked = false;
    bool hovered = false;
    bool pressed = false;
  };

  /// A deferred piece of drawing: a tooltip, a drill-in or a modal, emitted at EndFrame so it lands over everything.
  /// The text is COPIED rather than referenced, because a string_view handed to Tooltip may not outlive the call and
  /// a dangling one would be a defect that only showed as garbage on screen.
  struct Overlay
  {
    static constexpr std::size_t MAX_LINES = 8;
    static constexpr std::size_t MAX_LINE_CHARS = 72;

    Rect rect{};
    char lines[MAX_LINES][MAX_LINE_CHARS + 1] = {};
    std::uint8_t lineCount = 0;
    std::uint32_t borderRgba = 0;
    bool present = false;

    /// A modal carries its two buttons, so EndFrame can draw the panel and then the buttons over it. A tooltip or a
    /// drill-in leaves these empty.
    Rect confirmRect{};
    Rect cancelRect{};
    ButtonVisual confirmVisual{};
    ButtonVisual cancelVisual{};
    bool hasButtons = false;
  };

  [[nodiscard]] ButtonVisual ButtonBehavior(WidgetId _id, const Rect& _rect) noexcept;
  void DrawButton(const Rect& _rect, std::string_view _label, const ButtonVisual& _visual) noexcept;

  /// Copies lines into an overlay and sizes it to them.
  void FillOverlay(Overlay& _overlay, const Rect& _anchorRect, std::span<const std::string_view> _lines,
                   std::uint32_t _borderRgba) noexcept;
  void DrawOverlay(const Overlay& _overlay) noexcept;

  /// Whether this widget may take the mouse at all. False while a modal holds the frame.
  [[nodiscard]] bool Interactive() const noexcept;

  PrimitiveBatch& m_batch;
  TextRenderer& m_text;
  const InputState& m_input;
  std::array<WidgetId, MAX_ID_DEPTH> m_idStack{};
  std::size_t m_idDepth = 0;
  WidgetId m_hot = NO_WIDGET;
  WidgetId m_active = NO_WIDGET;
  WidgetId m_focus = NO_WIDGET;
  Overlay m_hint;  ///< the tooltip or drill-in, at most one a frame
  Overlay m_modal; ///< the confirmation, drawn over even that
  bool m_pressedSomething = false;
  bool m_modalWasOpen = false; ///< a modal held the frame BEFORE this one, so nothing behind it is interactive
  bool m_modalOpenThisFrame = false;
  bool m_inModal = false; ///< set while the modal's own widgets are being resolved
};

} // namespace Neuron
