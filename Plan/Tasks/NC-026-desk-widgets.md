# NC-026 — Desk widgets

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient | L | **yes** | no | Open |

**Depends on:** NC-025
**Read first:** GDD §3 (the board, the accusation panel, the composer, the plan editor: what each needs to show and take), §4 (hypothesis as selection; plan rules), §11 (the three governor policies)

## Goal

The rest of the widget set the desk session needs, and nothing it does not: a selectable list with scrolling, tabs, a numeric stepper, a toggle, a small text field for numbers, a drill-in tooltip for "one tap behind", and a modal confirmation. Each is derived from GDD §3's screens, and the task lists which screen needs which.

## Deliverables

- `Ui` gains: `List(id, rect, items, selectedIndex&, scroll&) -> bool changed` (the board's items, offers, the hypothesis readings), `Tabs(id, rect, labels, active&)` (the desk's screens), `Stepper(id, rect, value&, min, max, step)` (withdrawal percent, fuel to buy, wing counts), `Toggle(id, rect, label, on&)` (marked/unmarked, a governor's hold-or-evacuate), `NumberField(id, rect, value&)` (a price for the sell rule), `Tooltip(rect, lines)` shown while hovering and `Detail(id, anchorRect, lines)` shown on click (the report behind a projection), `Confirm(id, title, lines) -> Choice` (commit an operation, accept a contract), `Scrollbar`.
- `NeuronClient/UiState.h`: the small persistent state a caller keeps between frames (`ScrollState`, `FocusState`).
- `Main.cpp`'s pattern exercises every widget once.
- `NeuronClientTests/UiWidgetTests.cpp`: list selection by click and by keys, stepper bounds, number-field parsing, tooltip timing with synthetic frames.

## Acceptance criteria

- [ ] Each widget is exercised on a desktop and the report lists them.
- [ ] A list of two hundred items scrolls by wheel and by bar, and selection survives scrolling.
- [ ] A stepper never leaves its bounds and shows its unit in the label (R6: the unit is in the name and on the screen).
- [ ] `NumberField` accepts digits and backspace only and parses to an integer; nothing else in the desk types text.
- [ ] `Detail` is an opaque panel drawn last in the frame, so it is never under something.

## Verification

```powershell
x64\Debug\NomadCommander.exe
vstest.console.exe x64\Debug\NeuronClientTests.dll /Platform:x64
```

## Decisions to record

None (NC-025's ADR covers the model).

## Out of scope

Drag and drop, a text editor, icons (text labels are the icons), animation.

## Notes

- Map every widget to the GDD §3 line that needs it in a comment; a widget with no line is one the desk does not need.

## Report

_Filled in on hand-back._
