# Codebase constraints that shape these screens

Read with `AGENTS.md` §5, R12, R13 and `Plan/Tasks/NC-020` to `NC-026`, `NC-072`, `NC-073`, `NC-076`.

## Fixed by rule (an agent may not change these)

| Constraint | Source | Effect on the screens |
|---|---|---|
| Screen is **1920x1080** `R8G8B8A8_UNORM`, presented 1:1, no present scale, no intermediate target | R12; ADR-008; NC-020, NC-021 | The size the screens are authored at, since the owner decided it on 2026-09-16. `GLYPH_SCALE` is 3 and the cell is 24 px. The 80x45 cell grid is unchanged, so the layouts would still hold at 1280x720 with scale 2 if it were ever reversed. |
| Blending and samplers are a pass's own business; multisampling is unavailable | §5 (lifted 2026-09-16); NC-022 | **Changed.** Alpha is allowed now, so the gradients, soft shadows, glow and haze in `screens/02` may be built as drawn — the *Flat fallbacks* below became a menu rather than a requirement. `PipelineDefaults` still leaves blending off, so a pass that wants it says so. Text on panels still uses `discard`, which stays the cheaper path. MSAA remains impossible: DXGI will not multisample a flip-model back buffer and R12 forbids the intermediate target a resolve would need. |
| A camera and a mesh pipeline are permitted; 3D is still not sanctioned | §5 (lifted 2026-09-16); GDD §13, §15; R23 | **Changed.** §5 no longer forbids them, but the GDD outranks it on design and still says the 3D client waits, so 3D needs the owner to move GDD §13 and §15 first. A 2D map camera was never that question and is simply available. NC-072 keeps the precomputed integer pixel positions of UI §4 anyway: they are what makes the map land on whole pixels, which is R12's point. |
| Bitmap font 8x8, 96 glyphs, integer scales only | R13; NC-023 | Type is monospaced and sized 8*k px; the cell grid is the truth. The font covers 0x20-0x7E and nothing else, so **every string the client draws is ASCII** — there is no fallback glyph. The screens were authored with typographic characters; UI §6 carries the substitution table (`·`->`|`, `→`->`->`, `≤`->`<=`, `×`->`x`, `−`/`–`->`-`, `…`->`...`) and the PNGs still show the originals. Owner decision, 2026-09-16: ASCII only, rather than extending the font. |
| Icons are in scope; no drag and drop, no animation | NC-026 (icons added 2026-09-16) | **Changed.** NC-026 now ships an `IconAtlas.h`, monochrome and tinted, drawn through the glyph pipeline on the 24-px cell. The screens still spell roles as words (`HARBOUR`, `CHOKEPOINT`) and that reads well — an icon accompanies a label, it does not replace one. Spheres are drawn with primitives. |
| Widgets: panel, label, button, list, tabs, stepper, toggle, number field, tooltip, detail, confirm, scrollbar | NC-025, NC-026 | Every control on the screens maps to one of these. Nothing else is introduced. |
| Client shows only wire data, sends only `WireInput`s | R18; NC-071 | Reports, readings, weights, projections are the wire's. The map draws other empires only from reports. |
| Panels opaque, layered by draw order; Detail drawn last | NC-025, NC-026 | Cards on the map are drawn after lanes and spheres. |
| The executable ships alone | R13 | Palette and any sphere shading table are `constexpr` in headers. |

## Flat fallbacks for the aspirational parts of `screens/02`

Since blending was permitted on 2026-09-16 these are **options, not requirements**. Build the effect as drawn, or take a fallback here because it is cheaper or crisper; say which in the report either way.

- **Sphere**: three concentric filled discs (highlight, body, limb) from a `constexpr` table of three colours per empire slot; or a 24-triangle fan with per-vertex colour, since `PrimitiveVertex` carries a packed colour and interpolation across a triangle is not blending.
- **Ground shadow**: a filled ellipse in `#05070A`.
- **Empire haze**: omit, or a Bayer-dithered disc of the dim empire colour on the plane grid.
- **Dashed lines**: segments.
- **Starfield**: single-pixel points from a seeded `Random` (R16) — no file.

## Needs an owner decision

1. ~~**Screen size**~~ — decided 2026-09-16: 1920x1080 at `GLYPH_SCALE` 3. R12 carries it and **ADR-008** records it. The one thing still outstanding is that ADR's measurement: nobody has yet confirmed `GetClientRect` reports 1920x1080 unscaled on a real display (NC-020's desktop criterion).
2. **Third empire's name** (`Sedu Compact`) and the invented system names (`Harrow`, `Tessa Gate`, `Pale Anchor`, `Ashfall`, `Cinder Reach`, `Low Meridian`, `Sedu Hold`), per Roadmap A11 — list them in `KesselScenario.h`'s header comment for renaming.
3. **Sphere shading** — whether NC-072 may add a `constexpr` shading table (three colours per slot) to `Palette.h`, or whether nodes stay flat discs in v0.1.
