# ADR-012 — The UI model

**Status:** Accepted
**Date:** 2026-09-16
**Task:** NC-025 (owner-visible)
**Cites:** GDD §3, §13; AGENTS.md R9, R12, R13, §5; `Design/UI/UI-Spec.md` §1, §2; ADR-008, ADR-010

## Context

Every screen in Phase 5 — the situation board, the map, the composer, the plan editor, the receipt — is built on whatever this task decides. AGENTS.md R12 rules out an immediate-mode *helper layer* (no Dear ImGui, no third party at all under R14), so the widget set is homegrown, and the question is what shape it takes before eleven screens are written against it.

The GDD is unusually specific about what the interface is for. §13: "The interface presents decisions, not data." §3 gives the panels by name. The UI package gives the grid, the palette and the chrome down to the pixel. None of them says how a widget is written.

## Decision

**Immediate mode, in pixel space, on the 24-pixel cell grid.**

1. **There is no widget tree.** A screen is a function that runs every frame; it calls `Button` where a button goes and the call returns whether it was clicked. Nothing is constructed, destroyed, or kept in sync with a model.
2. **State between frames is three ids and nothing else**: which widget the mouse is over (`Hot`), which one a press started in (`Active`), and which one has the keyboard (`Focus`). Everything else is recomputed from the reports and beliefs the client was handed.
3. **Widgets are keyed by caller-supplied names**, hashed FNV-1a with the id above them on a `PushId` stack. The same call site is the same widget every frame; the same name under two parents is two widgets.
4. **The click protocol is press-inside-then-release-inside.** A press makes a widget active and gives it focus; a release inside fires; a release anywhere else does not. Sliding off before letting go takes a mis-click back.
5. **Hit order follows draw order**: the last widget to claim the mouse in a frame is the hot one, so a panel drawn over another takes its clicks.
6. **Pixels are integers everywhere.** `Rect` is four `std::int32_t`. UI §1 says nothing draws at a non-integer position, and a float in this type is how that rule would quietly stop being true.
7. **The cell is 24 pixels and the grid is 80×45**, which is the 8×8 font at `GLYPH_SCALE` 3 on the 1920×1080 screen (ADR-008).
8. **Panels are opaque and dim is a colour.** `Palette` carries UI §2 verbatim; `TEXT_DIM` and `TEXT_FAINT` are what an aged report and a disabled control are drawn in. Blending has been permitted since 2026-09-16 and this model still has no use for it — which is a fact, not a prohibition, and a widget that earns a blend may set one (§5).
9. **`Ui` knows nothing about the game** (R9): it draws rectangles and text and answers questions about the mouse. No fleet, no report, no empire crosses into `NeuronClient`.

## What this forecloses

**A retained widget tree**, and with it everything that comes free with one: automatic layout that reflows when content changes, a widget that animates itself between frames without the screen asking, accessibility metadata that persists, and the ability to ask "what widgets exist" without running a frame. If the desk ever wants a screen reader, this is the decision that will have to be revisited.

**A second font size that is not an integer scale.** Text is `GLYPH_SCALE` or a whole multiple; there is no 1.5×, because NC-023's glyph path Loads texels rather than sampling them and a fractional scale would need a sampler, which is what §5 says costs the 1:1 guarantee.

**Widget state that outlives a frame without somebody owning it.** A scroll position, a selected row, an open tab: the screen holds those, not `Ui`. That is the cost of immediate mode and it is paid in NC-026 and Phase 5, not here.

It does **not** foreclose: blending, a sampler, a widget that keeps its own cache, or a retained layer built on top later. Nothing about this is load-bearing for the renderer beneath it.

## Consequences

- `Rect.h`, `Palette.h`, `Ui.h`/`.cpp` in `NeuronClient`. `Ui` is constructed over a `PrimitiveBatch&`, a `TextRenderer&` and a `const InputState&` — it draws through the first two and reads the third, and owns none of them.
- **Three widgets and no more in this task**: `Panel`, `Label`, `Button`. NC-026 adds lists, scrolling, tabs, steppers, fields and tooltips, and every one of them is a function on `Ui` in the same shape.
- Because `Ui` needs no device to decide anything, its tests run without D3D12 at all: `PrimitiveBatch` and `TextRenderer` check their mapped pointer on every call and return, so hit testing and the click protocol are tested as the arithmetic they are.
- The 24-pixel cell and the 80×45 grid are now in code as `CELL_PIXELS`, `GRID_COLUMNS`, `GRID_ROWS`, and a test asserts they still multiply out to 1920×1080.

## Measurements

None quoted, and one worth naming as absent: **nobody has yet laid out a real screen with this.** The model is answerable to eleven screens in Phase 5 and has so far been asked to draw one panel, one label and one button. The first screen that fights it — most likely the plan editor of UI §6, which has the densest nesting — is the measurement, and NC-026 is where it will show.

The palette values are transcribed from UI §2 and a test asserts four of them byte for byte in the packing the scene target stores, which catches a transcription slip in the direction that matters: what reaches the glass.
