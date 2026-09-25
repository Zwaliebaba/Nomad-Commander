# ADR-009 — The scene target and the present scale

**Status:** Accepted
**Date:** 2026-09-16
**Task:** NC-021 (implements); NC-020 amended, NC-027 relieved; NC-029 measures
**Cites:** GDD §13 (v1.7); AGENTS.md R12, R13, R14, §5; ADR-008; `Design/UI/UI-Spec.md` §1

## Context

R12 said the game drew straight into the swap chain's back buffer, whose client area was exactly 1920×1080, with "no intermediate render target, no resolve pass and no present scale". That bought a real guarantee — a channel authored `0xAA` reached the glass as `0xAA`, and an 8×8 glyph landed on whole pixels — and it cost two things that had both become concrete by the end of 2026-09-16.

**It could not run on most displays.** The frame around a 1920×1080 client area makes the window about 1926×1117, which is taller than a 1080p screen before the taskbar takes its share. ADR-008 named this and accepted it; it is the most common PC display resolution, and any laptop at 125–150 % scaling is worse.

**It made anti-aliasing impossible, at the moment the game acquired 3D.** DXGI will not multisample a flip-model back buffer, and a resolve needs a target to resolve from — which that sentence forbade. So did a post-pass, and so did supersampling. GDD v1.7 then put a 3D map in v0.1, where every sphere has a silhouette and every lane has a slope, and there was no way to anti-alias any of it.

The owner asked for the intermediate target on 2026-09-16, to make the screen look right at different resolutions.

## Decision

**Every pass draws into a scene target of exactly 1920×1080 `R8G8B8A8_UNORM`** — not `_SRGB` — and the frame ends by presenting that target into the swap chain's back buffer, **scaled to the window's client area with the aspect ratio preserved** and letterboxed or pillarboxed as needed.

1. **The game never draws at any other size.** The 80×45 cell grid, `GLYPH_SCALE` 3 and every layout in UI §1 are unconditional. A pass that asks the window how big it is has misunderstood this decision; exactly one place knows, and it knows only how to fit one rectangle inside another.
2. **The present step takes the cheapest path available to it**: exactly 1:1 with no filtering when the client area is 1920×1080; point sampling at an exact integer multiple; bilinear otherwise. The common case stays pixel-perfect.
3. **The scene target may be multisampled** and resolved before the present step, because it is not a back buffer. Whether to pay for that is NC-027's to decide with a measurement.
4. **R13 is untouched.** The target is created, never loaded; no file appears beside the executable.

## What this forecloses

**The 1:1 guarantee, at the last step, whenever the scale is not 1.** This is the whole cost and it should not be understated: this client is dense with 8-pixel bitmap text, and resampling an 8×8 glyph by a non-integer factor is the one thing bitmap type is worst at. A player on a 1366×768 desktop gets the whole game at 0.71× and softer text than a player at 1920×1080 gets. Point sampling would keep it crisp and make glyph stems uneven instead; bilinear is the least-bad of two bad options and is why the integer and 1:1 cases are called out separately rather than left to a general filter.

It also forecloses the simplest possible frame loop — there is now a target to create, resize and transition, and a present pass with its own pipeline, sampler and root signature. That is the first sampler in the tree; §5 stopped forbidding one on the same day, and this is what it was needed for.

It does **not** foreclose going back: deleting the scene target and drawing into the back buffer restores the old rule exactly, at the cost of the two things above.

## Consequences

- `NC-021` grows the scene target, its RTV, the present pass and the fit arithmetic. Its `FrameTarget` becomes the same thing the game already uses rather than a test-only object, which simplifies A12.
- `NC-027` is relieved: MSAA is available without opening R12 again, and its ADR becomes "is it worth paying for" rather than "is it possible".
- **`NC-020` implements the *fit* policy**, chosen by the owner the same day. `Window::Create` gives the requested client area where the work area can hold a window around it, and otherwise the largest area of the same shape that it can; the window stays fixed and unresizable. A `DesktopTooSmall` fault covers a work area that cannot hold a window at all, and `FittedToDesktop()` tells the renderer whether it is scaling. The alternative — a resizable window with a `WM_SIZE` path and a swap-chain resize — was not taken and is not foreclosed.

  **This bullet is superseded by [ADR-010](ADR-010-the-borderless-window.md) (2026-09-16).** The first desktop run showed that *fit* guaranteed a resampled screen on a 1920×1080 display — 1729×973 at a scale of 0.9005 on the owner's machine — because no window with a caption can have a client area as tall as its monitor. The window is now borderless and covers the primary monitor, so on a 1920×1080 display the client area is the screen and the present step below takes its 1:1 path. **Everything else in this ADR stands**: the scene target, the three filter cases and the multisampling it unlocked are unchanged, and ADR-010 changes only which client areas the present step is handed.
- ADR-008's "What this forecloses" is relieved but not withdrawn: 1920×1080 is still what the game draws, and it is still what a display must have to see it unscaled.
- **Input is taken back through the same placement** (NC-033, added 2026-09-24). The mouse arrives in the client area's pixels, and `InputState` takes every point through `PresentPass::ScenePixelUnder`, which sits beside `Fit`, so `MousePosition()` is in scene pixels and every hit test agrees with the glass. This ADR did not say so, and nothing did it: every desktop run until then had been on a 1920×1080 monitor, where the present step copies and a client pixel *is* a scene pixel, so the first display that scaled — a 3:2 panel — put NC-024's crosshair about 3 cm from the pointer and every click with it. The decision above is unchanged: the present step is still the one place that knows where the scene is, and the mouse is handed its answer.

## Measurements

**Measured by NC-029**, three reports after this ADR asked for them. NC-021, NC-027 and NC-028 each recorded the figures as owed and each read the obligation as a *photograph*, which needs a display that is not 1920×1080; the numbers never did. A destination’s extent is a parameter rather than a property of the monitor — a back buffer’s comes from `DXGI_SWAP_CHAIN_DESC1` and a scene target’s from its `Desc` — so `PresentScaleTests` presents one scene target into three destinations of its own making, on WARP, and reads each back. The figures therefore reproduce on a CI runner with no display at all.

The sample is every printable glyph of all three faces (ADR-016) plus one line of ordinary prose, in `Palette::TEXT` on `Palette::BACKGROUND`. *Lit* is a pixel that is not the background; *partial* is lit but not the full ink — an anti-aliased edge rather than solid type. *Moved* is measured against the source texel each destination pixel stands over.

| | 1:1 — 1920×1080 | 2× point — 3840×2160 | 0.71× bilinear — 1366×768 |
|---|---|---|---|
| Placement | 1920×1080 at (0,0) | 3840×2160 at (0,0) | 1365×768 at (0,0), one bar column |
| Pixels in the placement | 2,073,600 | 8,294,400 | 1,048,320 |
| **Pixels moved** | **0** | **0** | **14,480** |
| Largest single-channel move | 0 | 0 | **151** of 255 |
| Colours the source does not contain | 0 | 0 | 6,530 |
| Lit pixels | 21,538 | 86,152 | 15,056 |
| Partial (edge) pixels | 15,905 | 63,620 | 14,669 |
| **Partial as a share of lit** | **73 %** | **73 %** | **97 %** |

**The first two columns are exact, not approximate.** The 1:1 path moves no pixel because it is a `CopyResource` and there is no sampler to round anything. The 2× column is the same image four times over — 86,152 is exactly 4 × 21,538 and 63,620 exactly 4 × 15,905 — so every texel really did become its own 2×2 block and point sampling introduced no colour the scene target did not already hold. That is `PresentPass.h`’s claim that "a glyph’s bit pattern stays a bit pattern, just bigger", measured rather than asserted.

**The third column is the cost, and 97 % is the number to quote.** After a 0.71× bilinear present only 3 % of the lit pixels are still full-strength ink, where 27 % were before; essentially every pixel carrying text moved (14,480 against 15,056 lit) and essentially none of the background did. A single channel moves by as much as 151 of 255, so this is not a subtle wash — an edge texel can land more than half way to the other colour. "Softer text" was the right words for it.

**What this does not settle, and is still owed:** whether that is *acceptable to look at*. These are pixels, not a judgement, and nobody has yet seen the 0.71× case on a real 1366×768 panel — the machine this was measured on has one 1920×1080 display, which is why the numbers had to be taken this way in the first place. **Note also that *What this forecloses* above is written about an 8×8 bitmap font that [ADR-016](ADR-016-the-desk-text-faces.md) has since replaced with anti-aliased coverage**, so its "the one thing bitmap type is worst at" overstates the cost as the client now stands. Rewriting that paragraph is the owner’s call; NC-029 supplied the figure and left the prose alone.
