# ADR-nnn — The screen is 1920×1080 at GLYPH_SCALE 3

**Status:** Draft (owner decision; not accepted)
**Date:** 2026-09-16
**Task:** NC-020, NC-021, NC-023, NC-025 (amendment)
**Cites:** GDD §13; AGENTS.md R12, R13, §5; Plan/Roadmap.md (UI model decision); Design/UI/UI-Spec.md §1

## Context
R12 fixes the client area at 1280×720, presented 1:1 with no present scale. The reference screens in `Design/UI/` are authored at 1920×1080. Both sizes share the 80×45 cell grid of the UI model (NC-025): 1280×720 at `GLYPH_SCALE` 2 (16 px cells) and 1920×1080 at `GLYPH_SCALE` 3 (24 px cells). The question is only which set of pixels the executable presents; the layout is the same.

## Decision
Either (a) keep 1280×720 and read the reference screens at 2/3, or (b) set `SCREEN_WIDTH_PIXELS = 1920`, `SCREEN_HEIGHT_PIXELS = 1080`, `GLYPH_SCALE = 3`, and amend R12's numbers. Nothing else in R12 changes: still `R8G8B8A8_UNORM`, still 1:1, still no present scale, still no intermediate target. The plan recommends (b) only if the owner's own desktop is at least 1920×1080 at 100 % scaling, because R12 forbids scaling the window down.

## What this forecloses
(b) forecloses running the client on a display smaller than 1920×1080 physical pixels, including most laptops at 125–150 % scaling, until a present scale exists — which R12 does not allow and which would need its own ADR. (a) forecloses nothing.

## Consequences
(b): `Window.h` constants, `SwapChainTarget`/`FrameTarget` `Desc` values through those constants, `TextRenderer::GLYPH_SCALE`, and the readback tests that assert sizes. `Rect::Cell` is unchanged (it takes the cell size from `GLYPH_SCALE`). All Phase 5 screens are unaffected because they lay out in cells.

## Measurements
None yet. Before accepting (b), record the owner's display size and scaling and confirm `GetClientRect` reports 1920×1080 with no system scaling (NC-020's acceptance criterion at the new size).
