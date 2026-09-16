# Codebase constraints that shape these screens

Read with `AGENTS.md` §5, R12, R13 and `Plan/Tasks/NC-020` to `NC-026`, `NC-072`, `NC-073`, `NC-076`.

## Fixed by rule (an agent may not change these)

| Constraint | Source | Effect on the screens |
|---|---|---|
| Screen is 1280×720 `R8G8B8A8_UNORM`, presented 1:1, no present scale, no intermediate target | R12; NC-020, NC-021 | 1920×1080 is **not** available to an agent. The screens are drawn on the 80×45 cell grid so they render identically at 1280×720 with `GLYPH_SCALE` 2. See `ADR-draft-screen-size.md`. |
| No blending, no multisampling, no sampler | §5; NC-022 | No alpha anywhere. Gradients, soft shadows, glow and haze in `screens/02` are aspirational: implement as flat fills, stepped rings or dithering (see *Flat fallbacks*). Text sits on panels via `discard`, not alpha. |
| No camera, no mesh pipeline, no step towards 3D | §5; NC-072 acceptance | The isometric map is legal **only** as precomputed integer pixel positions from the dimetric formula in UI §4. No transform in the shader beyond pixel→clip. |
| Bitmap font 8×8, 96 glyphs, integer scales only | R13; NC-023 | Type is monospaced and sized 8·k px. The mockups' proportional font is a stand-in; the cell grid is the truth. ASCII only: names must be ASCII. |
| Text labels are the icons; no drag and drop, no animation | NC-026 out-of-scope | Role glyphs are words (`HARBOUR`, `CHOKEPOINT`). Spheres are the one graphic element and are drawn with primitives. |
| Widgets: panel, label, button, list, tabs, stepper, toggle, number field, tooltip, detail, confirm, scrollbar | NC-025, NC-026 | Every control on the screens maps to one of these. Nothing else is introduced. |
| Client shows only wire data, sends only `WireInput`s | R18; NC-071 | Reports, readings, weights, projections are the wire's. The map draws other empires only from reports. |
| Panels opaque, layered by draw order; Detail drawn last | NC-025, NC-026 | Cards on the map are drawn after lanes and spheres. |
| The executable ships alone | R13 | Palette and any sphere shading table are `constexpr` in headers. |

## Flat fallbacks for the aspirational parts of `screens/02`

- **Sphere**: three concentric filled discs (highlight, body, limb) from a `constexpr` table of three colours per empire slot; or a 24-triangle fan with per-vertex colour, since `PrimitiveVertex` carries a packed colour and interpolation across a triangle is not blending.
- **Ground shadow**: a filled ellipse in `#05070A`.
- **Empire haze**: omit, or a Bayer-dithered disc of the dim empire colour on the plane grid.
- **Dashed lines**: segments.
- **Starfield**: single-pixel points from a seeded `Random` (R16) — no file.

## Needs an owner decision

1. **Screen size** — R12 and `SCREEN_WIDTH_PIXELS`/`SCREEN_HEIGHT_PIXELS`. Draft ADR attached.
2. **Third empire's name** (`Sedu Compact`) and the invented system names (`Harrow`, `Tessa Gate`, `Pale Anchor`, `Ashfall`, `Cinder Reach`, `Low Meridian`, `Sedu Hold`), per Roadmap A11 — list them in `KesselScenario.h`'s header comment for renaming.
3. **Sphere shading** — whether NC-072 may add a `constexpr` shading table (three colours per slot) to `Palette.h`, or whether nodes stay flat discs in v0.1.
