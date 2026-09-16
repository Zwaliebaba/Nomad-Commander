# Agent prompt — implementing the desk client screens

Paste the block below into the task file's **Read first** line of NC-072, NC-073, NC-076 (and the shared-chrome tasks NC-074, NC-075, NC-077, NC-078, NC-079), or give it to an agent directly with the task id.

---

## Prompt

You are implementing `NC-0nn` for Nomad Commander under `AGENTS.md`. Reference screens exist and are part of the design context:

- `Design/UI/screens/01-situation-board-1920x1080.png` — Board (NC-073)
- `Design/UI/screens/02-map-isometric-1920x1080.png` — Map (NC-072)
- `Design/UI/screens/03-operation-composer-plan-editor-1920x1080.png` — Composer and plan editor (NC-076)
- `Design/UI/UI-Spec.md` — grid, palette, per-screen anatomy, copy rules (cite as *UI §n*)
- `Design/UI/Codebase-Constraints.md` — what the engine can and cannot draw

Rules for using them:

1. **Match the screen for your task in layout, palette names, copy and reading order.** Column widths, the tab bar, the status line and the board strip are in UI §1 and §3-5 in cells; use those numbers. Where the PNG and UI-Spec disagree, UI-Spec wins; where UI-Spec and the GDD disagree, the GDD wins and you say so in the report.
2. **Draw at the screen size `Window.h` defines**, with `GLYPH_SCALE` as `TextRenderer.h` defines it. That is 1920x1080 at scale 3 (ADR-008). The screens are laid out on the 80x45 cell grid, so they would render unchanged at 1280x720 with scale 2 if it were ever reversed. R12 is the owner's to change, not yours.
3. **Palette names come from UI §2**, added to `Palette.h` as `inline constexpr` with the hex values given, commented `// UI §2`. Do not invent colours; if a state has no colour, use TEXT_DIM and note it.
4. **Aspirational effects are options, not requirements.** Gradient spheres, haze, glow and soft shadows in `screens/02` are rendered with blending in the mockup. Blending, samplers and a camera stopped being forbidden on 2026-09-16 (AGENTS.md §5), so you may build any of them as drawn, or take a flat fallback from `Codebase-Constraints.md`, or leave the node as a filled disc. Say which in the report. Two things you still may not do: multisample (DXGI will not, on a flip-model back buffer) and step towards 3D (GDD §13 and §15 still say it waits, and they outrank AGENTS.md on design).
5. **Every string on the screen is either the wire's or a template from UI §6, and every one of them is ASCII.** The font is 96 glyphs over 0x20-0x7E with no fallback, so use UI §6's substitutions rather than the typographic characters the PNGs show. No probability of a report being right; track records as two counts; projections as sentences; stakes before clicks.
6. **Widgets are NC-025/026's set**, which now includes icons. If a screen seems to need another, stop and put it in the report as a proposal.
7. **Report** under the task's *Report* section: which UI § each panel implements, where you deviated and why, what fallback you chose for each aspirational element, and a screenshot of the running client beside the reference PNG.

Acceptance addition for the task: *the owner opens the screen beside `Design/UI/screens/<n>.png` and recognises it row for row.*

---

## PR template addition

Add to `.github/pull_request_template.md` under the design-record question:

```
- [ ] If this PR touches a Phase 5 screen: which `Design/UI/UI-Spec.md` sections it implements, and one screenshot beside the reference PNG.
```

## Task file addition (example for NC-072)

```
**Read first:** ... ; Design/UI/UI-Spec.md §1, §2, §4; Design/UI/screens/02-map-isometric-1920x1080.png; Design/UI/Codebase-Constraints.md
```

and under **Acceptance criteria**:

```
- [ ] Rendered beside `Design/UI/screens/02-map-isometric-1920x1080.png`, the owner recognises the screen row for row; deviations are listed in the report with their UI § reference.
```
