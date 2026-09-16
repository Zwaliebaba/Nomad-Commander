# NC-025 — UI core

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient | L | **yes** | **yes** | Open |

**Depends on:** NC-022, NC-023, NC-024
**Read first:** GDD §3 whole (every panel it names), §13 ("The interface presents decisions, not data"); AGENTS.md R12 (no immediate-mode helper layers), R13 (colours embedded), §5 (no blending: panels are opaque)

## Goal

The homegrown widget layer every screen in Phase 5 is built from: an immediate-mode context over the primitive batch, the text renderer and the input state, with rectangles as layout, a palette compiled in, hover and click resolution, keyboard focus for the few fields that need it, and the three widgets everything else is made of: a panel, a label and a button. Rules out nothing the desk needs; adds nothing it does not.

## Deliverables

- `NeuronClient/Rect.h`: `struct Rect { std::int32_t x, y, width, height; }` with `Contains`, `Inset`, `SplitLeft/Right/Top/Bottom(pixels)`, `Cell(column, row)` on the 16-pixel grid.
- `NeuronClient/Palette.h`: `inline constexpr` colours with names from the design's vocabulary (`PANEL`, `PANEL_EDGE`, `TEXT`, `TEXT_DIM`, `ACCENT`, `WARNING`, `HOSTILE`, an entry per empire slot), cited from a comment; R13.
- `NeuronClient/Ui.h` + `.cpp`: `class Ui` constructed over `PrimitiveBatch&`, `TextRenderer&`, `const InputState&`; `BeginFrame()`/`EndFrame()`; `WidgetId` from a caller string hashed with the parent's id (FNV-1a), `PushId`/`PopId`; `Panel(rect, title)`, `Label(rect, text, color)`, `Button(id, rect, text) -> bool` with hover and pressed states; `Hot()`/`Active()` bookkeeping; `Focus` for keyboard.
- `Main.cpp`'s test pattern becomes a panel with a label and a button that counts clicks.
- `NeuronClientTests/UiTests.cpp`: hit testing and the click protocol (press inside, release inside fires; release outside does not) with synthetic input, no rendering needed; `Rect` arithmetic.

## Acceptance criteria

- [ ] The button fires exactly on press-and-release inside it, once, and shows hover and pressed states; the report says it was clicked on a desktop.
- [ ] A widget's id is stable across frames and unique per call site under `PushId`.
- [ ] Text is laid out on the 16-pixel cell grid at `GLYPH_SCALE` 2; nothing in `Ui` draws with a non-integer position.
- [ ] Panels are opaque and layered by draw order; no alpha anywhere (R12).
- [ ] The three widgets are the only ones in this task; NC-026 adds the rest.

## Verification

```powershell
x64\Debug\NomadCommander.exe        # panel, label, button; click it
vstest.console.exe x64\Debug\NeuronClientTests.dll /Platform:x64
```

## Decisions to record

**ADR — the UI model** (owner-visible). Recommendation (Roadmap): immediate mode in pixel space; the 8×8 font at scale 2 gives a 16-pixel cell and an 80×45 grid; widgets are functions on `Ui` keyed by caller ids; opaque panels because there is no blending; disabled and dim states are colours, not alpha. What it forecloses: a retained widget tree, animation by blending, a second font size that is not an integer scale.

## Out of scope

Lists, scrolling, tabs, steppers, text fields, tooltips (NC-026); any game vocabulary in NeuronClient (R9).

## Notes

- The "one tap behind" rule (GDD §13: the report, its source and its age one tap behind the decision) is a tooltip or a drill-in; NC-026 decides which, and both are opaque panels.

## Report

_Filled in on hand-back._
