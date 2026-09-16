# NC-025 — UI core

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient | L | **yes** | **yes** | Done (pending commit) |

**Depends on:** NC-022, NC-023, NC-024
**Read first:** GDD §3 whole (every panel it names), §13 ("The interface presents decisions, not data"); AGENTS.md R12 (no immediate-mode helper layers), R13 (colours embedded), §5 (blending is a pass's own business; panels are opaque by default)

## Goal

The homegrown widget layer every screen in Phase 5 is built from: an immediate-mode context over the primitive batch, the text renderer and the input state, with rectangles as layout, a palette compiled in, hover and click resolution, keyboard focus for the few fields that need it, and the three widgets everything else is made of: a panel, a label and a button. Rules out nothing the desk needs; adds nothing it does not.

## Deliverables

- `NeuronClient/Rect.h`: `struct Rect { std::int32_t x, y, width, height; }` with `Contains`, `Inset`, `SplitLeft/Right/Top/Bottom(pixels)`, `Cell(column, row)` on the 24-pixel grid.
- `NeuronClient/Palette.h`: `inline constexpr` colours with names from the design's vocabulary (`PANEL`, `PANEL_EDGE`, `TEXT`, `TEXT_DIM`, `ACCENT`, `WARNING`, `HOSTILE`, an entry per empire slot), cited from a comment; R13.
- `NeuronClient/Ui.h` + `.cpp`: `class Ui` constructed over `PrimitiveBatch&`, `TextRenderer&`, `const InputState&`; `BeginFrame()`/`EndFrame()`; `WidgetId` from a caller string hashed with the parent's id (FNV-1a), `PushId`/`PopId`; `Panel(rect, title)`, `Label(rect, text, color)`, `Button(id, rect, text) -> bool` with hover and pressed states; `Hot()`/`Active()` bookkeeping; `Focus` for keyboard.
- `Main.cpp`'s test pattern becomes a panel with a label and a button that counts clicks.
- `NeuronClientTests/UiTests.cpp`: hit testing and the click protocol (press inside, release inside fires; release outside does not) with synthetic input, no rendering needed; `Rect` arithmetic.

## Acceptance criteria

- [x] The button fires exactly on press-and-release inside it, once, and shows hover and pressed states; the report says it was clicked on a desktop.
- [x] A widget's id is stable across frames and unique per call site under `PushId`.
- [x] Text is laid out on the 24-pixel cell grid at `GLYPH_SCALE` 3; nothing in `Ui` draws with a non-integer position.
- [x] Panels are opaque and layered by draw order; no alpha anywhere (R12).
- [x] The three widgets are the only ones in this task; NC-026 adds the rest.

## Verification

```powershell
x64\Debug\NomadCommander.exe        # panel, label, button; click it
vstest.console.exe x64\Debug\NeuronClientTests.dll /Platform:x64
```

## Decisions to record

**Written: [ADR-012](../../Design/ADR/ADR-012-the-ui-model.md) — the UI model** (owner-visible). It takes the roadmap's recommendation unchanged. Recommendation (Roadmap): immediate mode in pixel space; the 8×8 font at scale 3 gives a 24-pixel cell and an 80×45 grid on the 1920×1080 screen; widgets are functions on `Ui` keyed by caller ids; panels opaque by default, with blending available to a widget that earns it; disabled and dim states are colours by default, and alpha is no longer ruled out. What it forecloses: a retained widget tree, a second font size that is not an integer scale.

## Out of scope

Lists, scrolling, tabs, steppers, text fields, tooltips (NC-026); any game vocabulary in NeuronClient (R9).

## Notes

- The "one tap behind" rule (GDD §13: the report, its source and its age one tap behind the decision) is a tooltip or a drill-in; NC-026 decides which, and both are opaque panels.

## Report

**Owner-visible, and the decision is [ADR-012](../../Design/ADR/ADR-012-the-ui-model.md).** Immediate mode in pixel space on the 24-pixel cell grid, widgets keyed by hashed caller names, panels opaque, dim a colour rather than an opacity. It takes the roadmap's recommendation without changing it, and its *What this forecloses* is the part worth reading: no retained tree, no fractional font scale, and widget state that outlives a frame belongs to the screen rather than to `Ui`.

**`Plan/README.md` says an owner-visible task lands on its own before anything is built on it.** NC-026 stacks directly on this and was built in the same sitting because that is what was asked for. The deviation is stated here rather than bent quietly; ADR-012 is a separate file precisely so it can still be read and reversed on its own.

**Built, run and clicked.** The demo panel has a label and two buttons, laid out in cells. A synthetic click was driven through the real message path — `SetCursorPos`, `mouse_event` down, `mouse_event` up — and the counter was then read **off the screen as pixels**, decoded against the font:

| | |
|---|---|
| before | the `0` glyph, bit for bit |
| after one click | the `1` glyph |
| after two | the `2` glyph |

So the whole chain works end to end: window message → `InputState` → the click protocol → the counter → `TextRenderer` → scene target → present → glass, and it fires **exactly once a click**.

**The three states are the palette, measured on the running game**, not asserted from the source:

| State | Fill | Border |
|---|---|---|
| rest | `#141922` PANEL | `#2A3140` PANEL_EDGE |
| hover | `#181F2A` PANEL_SELECTED | `#D9A441` ACCENT |
| pressed | `#D9A441` ACCENT | `#D9A441` ACCENT |

**Refined against the code as it is.**

- **The palette is `Design/UI/UI-Spec.md` §2 transcribed, and nothing here invented a colour.** The task said "names from the design's vocabulary"; the UI package turned out to give the values too, so `Palette.h` carries them and a test asserts four of them byte for byte **in the packing the scene target stores**, which is the direction a transcription slip would actually hurt.
- **`Palette` is a type, not a namespace.** R9 gives this layer one namespace and `Neuron` is it, so `Palette` is a struct of static constants in the shape `PipelineDefaults` already set.
- **`Rect` uses `xPixels`/`widthPixels`, not `x`/`width`.** The task spelled them without units; R6 says units belong in names and this type is never anything but pixels — a reader who assumed cells would be wrong by a factor of 24.
- **`Split*` mutate and return what they took.** `const Rect header = body.SplitTop(CELL_PIXELS * 2);` leaves `body` as what is left underneath, which is the layout idiom the desk wants; a const version returning both halves needs an out parameter at every call site.
- **`Contains` is half-open on both axes**, matching what `FillRect` fills, so what a hit test claims and what the screen shows are the same pixels. Tested at all four edges.
- **The id stack is fixed at sixteen deep** and `PushId` allocates nothing: the whole UI runs through that path every frame.
- **The `Ui` tests need no GPU.** `PrimitiveBatch` and `TextRenderer` check their mapped pointer on every call and return, so hit testing and the click protocol are tested as the arithmetic they are — eleven tests that run in under a millisecond each and would still catch a protocol change.

**Two Windows traps worth writing down, because both cost a build.**

- **`small` is a macro.** `rpcndr.h`, which `<windows.h>` drags in, defines `small` as `char`, so `Rect small{0, 0, 10, 10};` compiles as `Rect char{...}` and the error names neither. `NOGDI` and the rest of the macro family in `NeuronCore.h` do not exclude it.
- **`CheckProjectFiles` reads `AdjacentRectangles` as containing "centre"** — `Adja`·`centRe`·`ctangles` across the camelCase boundary — and fails R11. A false positive, and a cheap one to work around by renaming, but worth knowing before somebody spends ten minutes on it. Left alone rather than fixed: loosening the check to respect word boundaries risks missing a real `Centre`, and this task is not the one to weaken a checker.

**clang-tidy found four** `readability-identifier-naming` on `constexpr` locals in the tests, which R3 makes UPPER_CASE. They became `const`, because a compile-time constant is not what they were for and `RECT` in capitals is already a Windows type.

**Verified:** `CheckFormat.py` (90 files), `CheckProjectFiles.py` (9 projects, clean), `RunClangTidy.py` (**41 translation units clean**). Debug **and** Release rebuild with zero warnings. All four suites: **129 of 129 green**, 51 in `NeuronClientTests`, 15 new here.

**Not done, and named.** The keyboard focus is tracked and cleared but nothing consumes it yet: there is no widget in this task that takes typing, so `Focus` is pinned by a test and exercised by nothing. NC-026's text field is what makes it real. Lists, scrolling, tabs, steppers and tooltips are out of scope and stay out.
