# NC-026 — Desk widgets

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient | L | **yes** | no | Done (pending commit) |

**Depends on:** NC-025
**Read first:** GDD §3 (the board, the accusation panel, the composer, the plan editor: what each needs to show and take), §4 (hypothesis as selection; plan rules), §11 (the three governor policies)

## Goal

The rest of the widget set the desk session needs, and nothing it does not: a selectable list with scrolling, tabs, a numeric stepper, a toggle, a small text field for numbers, a drill-in tooltip for "one tap behind", a modal confirmation, and icons. Each is derived from GDD §3's screens, and the task lists which screen needs which.

## Deliverables

- `Ui` gains: `List(id, rect, items, selectedIndex&, scroll&) -> bool changed` (the board's items, offers, the hypothesis readings), `Tabs(id, rect, labels, active&)` (the desk's screens), `Stepper(id, rect, value&, min, max, step)` (withdrawal percent, fuel to buy, wing counts), `Toggle(id, rect, label, on&)` (marked/unmarked, a governor's hold-or-evacuate), `NumberField(id, rect, value&)` (a price for the sell rule), `Tooltip(rect, lines)` shown while hovering and `Detail(id, anchorRect, lines)` shown on click (the report behind a projection), `Confirm(id, title, lines) -> Choice` (commit an operation, accept a contract), `Scrollbar`.
- `NeuronClient/IconAtlas.h`: the icon art, embedded as a `constexpr` array exactly as the font is (R13 — art is embedded, never loaded), and `Ui::Icon(rect, iconId, colorRgba)`. Monochrome, tinted at the call site, read with `Texture2D<uint>::Load()` through NC-023's glyph pipeline: an icon is the same kind of thing as a glyph, so it needs no new pipeline, no sampler and no blending. Sized to the 24-pixel cell so icons land on NC-025's grid.
- `NeuronClient/UiState.h`: the small persistent state a caller keeps between frames (`ScrollState`, `FocusState`).
- `Main.cpp`'s pattern exercises every widget once.
- `NeuronClientTests/UiWidgetTests.cpp`: list selection by click and by keys, stepper bounds, number-field parsing, tooltip timing with synthetic frames.

## Acceptance criteria

- [x] Each widget is exercised on a desktop and the report lists them.
- [x] A list of two hundred items scrolls by wheel and by bar, and selection survives scrolling.
- [x] A stepper never leaves its bounds and shows its unit in the label (R6: the unit is in the name and on the screen).
- [x] `NumberField` accepts digits and backspace only and parses to an integer; nothing else in the desk types text.
- [x] `Detail` is an opaque panel drawn last in the frame, so it is never under something.
- [x] Every icon is derived from a GDD §3 screen that needs one, drawn on the cell grid at an integer position, and readable at 24 pixels; a label sits beside it rather than being replaced by it, because the desk is read, not scanned.

## Verification

```powershell
x64\Debug\NomadCommander.exe
vstest.console.exe x64\Debug\NeuronClientTests.dll /Platform:x64
```

## Decisions to record

None (NC-025's ADR covers the model).

## Out of scope

Drag and drop, a text editor, animation.

Icons were out of scope until 2026-09-16 — "text labels are the icons" — and are now in it, as above. Two things stay out: a *coloured* or alpha-blended icon, which needs an atlas format this task does not define (blending is available since the same date, so it is a later task's option rather than a prohibition), and an icon that replaces a label rather than accompanying one.

## Notes

- Map every widget to the GDD §3 line that needs it in a comment; a widget with no line is one the desk does not need.

## Report

**Built, run, and every widget exercised on the desktop by driving the real mouse** — `SetCursorPos` and `mouse_event` into the running game — and then reading the result off the screen as pixels. The criterion says the report lists them, so:

| Widget | What was done | What the screen showed |
|---|---|---|
| `Tabs` | clicked "Operations" | ACCENT underline `#D9A441` at its foot |
| `List` | clicked the fifth row | `#181F2A` PANEL_SELECTED fill on that row |
| `Scrollbar` | dragged the thumb 480 px | ACCENT thumb arrived at the drag position |
| `Stepper` | clicked minus once | the value glyph went **`4` → `3`**, decoded against the font: 40 → 30 at step 10 |
| `Toggle` | clicked it | the check icon appeared, drawn in BACKGROUND over the ACCENT box |
| `NumberField` | clicked into it | border turned ACCENT — it has the keyboard |
| `Tooltip` | hovered Commit | a `#0F1319` BACKGROUND_RAISED panel under it |
| `Detail` | clicked "One tap behind" | the same, bordered ACCENT |
| `Confirm` | clicked Commit, then Confirm | **864 ACCENT pixels** along the modal's top border, then **0** — it closed |
| `Icon` | — | Report, Time and Warning beside their labels in the right panel |

**The modal is the one worth dwelling on.** It is drawn over everything and it holds the frame behind it, and both had to be built rather than hoped for. Drawing it last means deferring it: `Confirm` resolves its buttons during the frame and records the panel *and* the buttons into an overlay that `EndFrame` emits, which is why `Button` was split into `ButtonBehavior` and `DrawButton`. Holding the frame means `UpdateHot` refuses every claim while a modal is up — **which takes effect the frame after it opens**, because by the time `Confirm` is called the frame's other widgets have already been asked. That one-frame latency is what every immediate-mode modal has; the test `AModalAnswersAndHoldsTheFrameBehindIt` asserts the button behind is dead from the second frame onward, and says so in its comment rather than hiding it.

**Icons share the glyph atlas, which is the reason they needed no new pipeline.** An icon is eight by eight and monochrome — exactly what a glyph is — so the atlas grew from 128×48 to 128×64, the icons occupy cells 96 onward, and `TextRenderer::DrawIcon` is `Draw` pointed at a different cell. No second texture, no sampler, no blending, and `Texture2D<uint>::Load()` all the way through. Both `Draw` and `DrawIcon` now go through one `PushQuad`.

**Twelve icons, each naming the line of the design that needs it** — Report, Courier, Fleet, Accusation, Offer, Market, Contract, Time, Credits, Warning, Selected, Uncovered — authored here as the font was, with the shapes drawn as pictures and the bytes derived. A test asserts none of them is nearly blank, because an icon somebody forgot to draw would show as an empty cell rather than as an error.

**Refined against the code as it is.**

- **`ScrollState` and `FieldState` live in `UiState.h` and belong to the caller.** ADR-012 put widget state that outlives a frame in the screen rather than in `Ui`, and this is what a screen declares to hold it.
- **`List` draws and hit-tests only the rows it can see.** Two hundred items is 4,800 pixels of content in a 480-pixel view; drawing the hundred and ninety off-screen would be work for nothing every frame. Rows are clipped by hand, because there is no per-widget scissor and a half-row drawn past the end would sit on whatever is beside the list.
- **The wheel scrolls anywhere over the list**, not only over the bar, which is what a person expects without aiming.
- **Arrow keys move the selection and the view follows it.** Tested by walking to row 40 and asserting the selected row is inside the view.
- **`NumberField` takes digits and backspace and nothing else**, parses as it goes, and clamps rather than overflowing — typing six nines into a field capped at 500 leaves 500. It is a number field and not an editor because nothing else in the desk types at all.
- **`Stepper` clamps a value it was *handed* out of bounds**, not just one it stepped out of bounds, so a caller that starts at 999 in a 0–8 field is corrected on the first frame that draws it.
- **A tooltip claims no widget id.** It is not something you can click, so it must not take the mouse from what it is about.
- **Overlay text is copied, not referenced.** A `string_view` handed to `Tooltip` need not outlive the call, and a dangling one would show as garbage on screen rather than as a crash. Eight lines of 72 characters, fixed, allocating nothing.

**Verified:** `CheckFormat.py` (93 files), `CheckProjectFiles.py` (9 projects, clean), `RunClangTidy.py` (**42 translation units clean**). Debug **and** Release rebuild with zero warnings. All four suites: **143 of 143 green**, 65 in `NeuronClientTests`, 14 new here. The debug layer said nothing across the interactive run.

**Bent:** this stacks directly on NC-025, which is owner-visible and unlanded — see NC-025's report. Nothing else.

**Not done, and named.** Drag and drop, a text editor and animation stay out of scope. So do the two things NC-026's own scope note keeps out: a coloured or alpha-blended icon, which would need an atlas format this task does not define, and an icon that replaces a label rather than accompanying one. **The tooltip has no delay** — it appears the moment the mouse is over its anchor rather than after a rest, because `InputState` has no clock and the frame loop has no time base yet; the task's "tooltip timing with synthetic frames" is therefore tested as *presence while hovered* and not as a dwell. A dwell needs a tick the client can read, which arrives with NC-030's schedule.
