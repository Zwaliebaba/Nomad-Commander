# ADR-008 — The screen is 1920×1080 at `GLYPH_SCALE` 3

**Status:** Accepted. `GLYPH_SCALE` itself is gone — [ADR-016](ADR-016-the-desk-text-faces.md) replaced the bitmap font with baked faces — but the screen and the cell this decided are unchanged: 1920×1080, a 24-pixel cell, an 80×45 grid. Read "`GLYPH_SCALE` 3" below as "a 24-pixel cell".
**Date:** 2026-09-16
**Task:** NC-020 (implemented); NC-021, NC-023, NC-025 amended
**Cites:** GDD §13; AGENTS.md R12, R13, §5; `Design/UI/UI-Spec.md` §1; `Plan/Roadmap.md` (the UI model)

## Context

This ADR is `Design/UI/ADR-draft-screen-size.md` decided. That draft put the question as a choice: (a) keep the 1280×720 that R12 had fixed and read the reference screens at two thirds, or (b) move the screen to the 1920×1080 the screens are authored at. Both sizes carry the same 80×45 cell grid — 1280×720 at `GLYPH_SCALE` 2 with a 16-pixel cell, 1920×1080 at `GLYPH_SCALE` 3 with a 24-pixel cell — so the layout was never what was at stake. Only which pixels the executable presents.

The owner chose (b) on 2026-09-16 and it is implemented: `SCREEN_WIDTH_PIXELS` and `SCREEN_HEIGHT_PIXELS` in `NeuronClient/Window.h`, R12's numbers, and every document that quotes them.

## Decision

The client area is **1920×1080 physical pixels** and `GLYPH_SCALE` is **3**. Nothing else in R12 moves: still `R8G8B8A8_UNORM` rather than `_SRGB`, still drawn straight into the swap chain's back buffer, still presented 1:1, still no intermediate render target, no resolve pass and no present scale.

`Plan/Roadmap.md`'s UI-model recommendation and NC-023's `GLYPH_SCALE` move from 2 to 3 with it. 1080 is not divisible by 16, so a 16-pixel cell would leave 67½ rows; 1920×1080 is exactly 1.5× the old screen, so a 24-pixel cell restores the same 80×45 grid with larger glyphs and no layout changes anywhere.

## What this forecloses

**Running the client on a display smaller than 1920×1080 physical pixels, which is most of them.** The frame adds a caption and borders, putting the window near 1926×1117 — taller than a 1080p screen before the taskbar takes its share — and R12 forbids scaling it down. The draft named this and it is the real cost: the overhang case went from CI runners and old hardware to the most common PC display, and to any laptop at 125–150 % scaling.

**ADR-009 relieves this without withdrawing it** (same day). A scene target and a present scale mean a display too small to hold 1920×1080 can still show the whole game, smaller. 1920×1080 remains what the game *draws*, and remains what a display must have to see it unscaled.

It also forecloses nothing about the *layout*, which is the point of deciding it on the cell grid: reversing to 1280×720 at `GLYPH_SCALE` 2 would touch two constants and no screen.

What the game should do on a desktop that cannot hold its screen — overhang, as it does today, or refuse to start and say so — this ADR does not decide. R12 records it as open and the first task that needs an answer writes it.

## Consequences

- `NeuronClient/Window.h`'s two constants. `Window` itself needed no change: it is written against its `Desc`, and the `WM_GETMINMAXINFO` override from NC-020 already tells Windows the desktop is not a limit on the window.
- `NC-021`'s `SwapChainTarget` and `FrameTarget` take their size through those constants; their readback tests assert them rather than literals.
- `TextRenderer::GLYPH_SCALE` was 3, and ADR-016 removed it; `Rect::Cell` is unchanged either way, and the faces are now baked to the cell rather than scaled into it.
- Every Phase 5 screen is unaffected — they lay out in cells.
- `WindowTests` now overshoot the CI runner's 1024×768 desktop on both axes instead of one, which is a harder case than the one that was red for four rounds.

## Measurements

The five `WindowTests` assert a client area of exactly 1920×1080 and pass on the GitHub Actions Windows runner, whose desktop is 1024×768, in [run 20](https://github.com/Zwaliebaba/Nomad-Commander/actions/runs/35113626709) — 86 of 86 tests green, Debug|x64, MSVC 14.51.36231.

**The draft's own precondition is still unmet and this ADR does not claim otherwise.** It asked for the owner's display size and scaling to be recorded, and for `GetClientRect` to be confirmed at 1920×1080 with no system scaling on a real display. Nobody has yet run `NomadCommander.exe` on a desktop; NC-020 carries that as an open acceptance criterion. Until it is done, this decision is verified on a build agent and nowhere else.
