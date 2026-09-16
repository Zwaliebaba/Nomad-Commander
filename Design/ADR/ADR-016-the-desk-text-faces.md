# ADR-016 — The desk's text faces: IBM Plex Mono, baked as coverage

**Status:** Accepted
**Date:** 2026-09-16
**Task:** NC-028 (owner-visible)
**Cites:** GDD §3, §13; AGENTS.md R12, R13, R14, §5; `Design/UI/UI-Spec.md` §1, §2, §6; ADR-008, ADR-009, ADR-012; supersedes the font half of NC-023

## Context

NC-023 authored an 8×8 bitmap font in this repository, 96 glyphs of one bit a pixel, and drew it at `GLYPH_SCALE` 3 so that a character occupied a whole 24-pixel cell. Two things about it were measured on 2026-09-16 and neither is a matter of taste.

**The font data had a defect the tests could not see.** All 26 capitals and all 10 digits ended on row 5; every lowercase letter sat on row 6. So `Kessel` and `1140 cr` stepped three pixels at the baseline at scale 3. The same split ran through the punctuation: `% & / ? @ [ \ ]` ended with the capitals, `! # $ ( ) < > { | }` with the lowercase. NC-023's readback tests compared the screen against those same bytes, so they were green over a font with two baselines. Beside that, the x-height was five rows against a six-row capital, which reads as small caps, and every stroke was two pixels wide, which reads as bold.

**The advance did not fit the design.** `Design/UI/` was authored in a browser at IBM Plex Mono 13–14 px scaled 1.5×, which is a 12-pixel advance. The bitmap font advanced 24. Every reference screen therefore held about twice the text per line that the engine could draw, and NC-072, NC-073 and NC-076 each carry the criterion *the owner recognises the screen row for row*. That criterion was unmeetable as written, and the side panel of `screens/02` is the proof: 46 characters a line against 22.

The moment to decide was now rather than later: no screen draws real text yet, so the cost is one task, and after Phase 5 it is eleven.

## Decision

**The desk's type is IBM Plex Mono, rasterized offline into coverage and compiled in. There are three faces, named for their role.**

1. **Three faces, and they are named by role rather than by size or weight** (`Font` in `Font.h`): `Body` is Regular at 20 px and advances 12; `Small` is Regular at 16⅔ px and advances 10; `Title` is SemiBold at 20 px and advances 12. A screen says what a line *is*, and the bake decides what that looks like — re-baking `Title` at another weight touches no call site.
2. **Every face is baked to a 24-pixel line**, so a row of text is exactly the cell of UI §1 and `Rect::Cell` is unchanged. A `static_assert` in `Ui.h` keeps that true. The 80×45 cell grid, the 1920×1080 screen and every layout in the UI spec are untouched; what changed is that a Body character is half a cell wide instead of a whole one.
3. **The rasterizing happens in `Tools/BakeFont.py`, by hand, and its output is committed** as `NeuronClient/FontData.h` (the metrics) and `NeuronClient/FontCoverage.h` (78,336 bytes of coverage). The build needs neither the tool nor Pillow nor a `.ttf`; CI runs exactly as before. R13 holds in the strict sense — the executable still ships alone and reads nothing.
4. **A glyph is one byte of coverage a texel and is drawn at the size it was baked**, one texel a pixel. The atlas is `R8_UNORM`, read with `Texture2D<float>::Load()`. There is still no sampler anywhere in the glyph path and nothing is ever scaled.
5. **The glyph pass blends, and it is the only pass that does.** The pixel shader emits the text colour with the coverage as alpha; `GlyphPipeline` overrides `PipelineDefaults::Blend()` with straight alpha over the destination and changes nothing else. A texel of no coverage is still `discard`ed. AGENTS.md §5 has permitted this since 2026-09-16: *blending is a pass's own business*.
6. **Icons keep their 8×8 art and their crispness.** `ICON_8X8_ART` is unchanged; a set bit becomes a 3×3 block of full coverage, so an icon is still the pixel art it was drawn as, still a cell square, still tinted at the call site.
7. **The licence travels with the bytes.** IBM Plex Mono is under the SIL Open Font License 1.1 with the reserved font name "Plex". The baked atlas is a modified version under that licence: `FontCoverage.h` carries the licence text verbatim, names its sources with their SHA-256 and their version string, and the artifact is not itself called Plex.

`BitmapFont.h` is deleted. `GLYPH_SCALE`, `GLYPH_WIDTH_PIXELS` and `GLYPH_HEIGHT_PIXELS` are gone with it; `TextRenderer::Draw` and `Measure` take a `Font` where they took a scale.

## What this forecloses

**An arbitrary text size.** A face is a baked artifact, so a new size means running the tool, committing bytes and adding an enumerator — deliberately more friction than a float would be. A screen that wants a fourth size is a proposal in a report, and the bake is where it is answered.

**Editing a glyph by moving a `1`.** NC-023's binary literals could be read as pictures and changed in place. Coverage cannot; the shapes now belong to the typeface and the tool. That is the trade: a designed face against an editable one.

**Byte-for-byte reproducibility across rasterizers.** The output depends on Pillow and FreeType, so a different version may round a stem differently and a regeneration shows as a diff. The headers are the artifact and are reviewed as one, which is why the versions are recorded beside the bytes. Nothing in the simulation is affected — R16 binds `GameLogic`, and this is the renderer.

**R14's closed dependency list is not reopened, and this is the line worth being precise about.** Pillow is a development tool that never ships and never runs in CI, in the same category as the checkers in `Build/`. The *font* is third-party content compiled in, which R13 has always allowed for art — but R13's own wording ("authored here, or a set published as public domain with its source named; anything under a licence needs owner approval first", NC-023) makes this the owner's call rather than an implementer's, and the owner made it on 2026-09-16.

It does **not** foreclose: multisampling (a glyph is still an axis-aligned quad on integer boundaries), a fourth face, re-baking at another size if the screen ever moves, or going back — `BitmapFont.h` is one revert away and nothing outside `TextRenderer` and `Font` knows how a glyph is stored.

## Consequences

- **New:** `Tools/BakeFont.py`, `NeuronClient/Font.h` + `.cpp`, and the generated `FontData.h` and `FontCoverage.h`. **Deleted:** `NeuronClient/BitmapFont.h`.
- `TextRenderer` uploads one atlas holding the three faces stacked and a row of icons, and takes a `Font` rather than a scale. `GlyphPS.hlsl` reads `Texture2D<float>` and returns coverage as alpha. `GlyphPipeline` turns blending on.
- `Ui::Label` takes a `Font`, defaulting to `Body`; `Ui::Panel` draws its title in `Title`. Every other widget is unchanged, because they lay out in cells and a cell did not move.
- `ADR-012`'s point 7 and its foreclosure of "a second font size that is not an integer scale" are superseded by this. The rest of ADR-012 stands: immediate mode, the cell grid, opaque panels, dim as a colour.
- `ADR-008` stands whole — the screen is still 1920×1080 — but `GLYPH_SCALE` is no longer the thing that makes the cell 24 pixels. The bake is.
- Phase 5's screens become buildable against `Design/UI/` as drawn. The *row for row* criterion in NC-072, NC-073 and NC-076 is now meetable, which it was not.
- If this is reversed: revert the four files, restore `BitmapFont.h`, and put the scale back in `TextRenderer` and `Ui`. No screen exists that would have to be re-laid-out, which is the whole reason it was decided before Phase 5 rather than during it.

## Measurements

**The two-baseline defect**, counted from `BitmapFont.h`'s own bytes on 2026-09-16 by parsing the binary literals and taking each glyph's last non-zero row:

| Set | Last row |
|---|---|
| `A`–`Z`, `0`–`9` | 5 |
| `a`–`z` without descenders | 6 |
| `g p q y` | 7 |

**The advance, against the reference screens.** IBM Plex Mono at 20 px measured 12.0 px a character through `Graphics.MeasureString` over ten characters. The side panel of `screens/02` holds 46 characters a line at 624 px; at a 24-pixel advance it holds 22 hlyphs, and at 12 it holds 46. The line *4 haulers, 2 raiders, escort light, heading Kessel* is two lines in the mockup and was three at 24.

**What was baked**, printed by the tool:

| Face | Source | Size | Advance | Baseline | Cap | x-height |
|---|---|---|---|---|---|---|
| Body | Regular | 20 px | 12 | 19 | 14 | 10 |
| Small | Regular | 16⅔ px | 10 | 18 | 12 | 9 |
| Title | SemiBold | 20 px | 12 | 19 | 14 | 10 |

78,336 coverage bytes for all three faces. No glyph of any face has ink outside its cell — the tool renders into a canvas three cells wide and reports any that would be clipped, and none was.

**Verified on this machine**, Debug and Release, x64: `CheckFormat.py` 115 files, `CheckProjectFiles.py` 9 projects clean, `RunClangTidy.py` 54 translation units clean, four suites 174 of 174 green (75 in `NeuronClientTests`). The executable was run on a 1920×1080 monitor and photographed at physical resolution: the three faces each draw the whole printable set, the icons are unchanged, and `One tap behind` fits the button it was clipped inside before.

**The readback tests changed shape and are stronger for it.** `EveryGlyphOfEveryFaceMatchesItsCoverage` draws all 96 glyphs of all three faces and compares every pixel against the baked byte blended over the background, per channel. `PartialCoverageBlendsTheInkTowardWhatIsUnderIt` draws `g` in a colour with three different channels and checks the anti-aliased edge, which is the property that did not exist before. A full or empty texel must match exactly; a partial one is allowed one level, which is the rounding a blend unit may take.
