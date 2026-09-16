# NC-023 — The bitmap font and text

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient | M | **yes** | no | Done (pending commit) |

**Depends on:** NC-022
**Read first:** AGENTS.md R13 ("a bitmap font is 96 glyphs, 8×8, one bit a pixel, 768 bytes, and nothing to load"), §5 (*A sampler on text costs the 1:1 guarantee* — why the glyph path uses `Texture2D<uint>::Load()`), R14

## Goal

Text on screen from data compiled into the executable: the 96 printable ASCII glyphs as a `constexpr` array, uploaded once to an integer texture, drawn as quads by a second pipeline whose pixel shader loads texels and discards the zeros. Integer scaling only, so text stays crisp at 1:1.

## Deliverables

- `NeuronClient/BitmapFont.h`: `inline constexpr std::array<std::uint8_t, 768> FONT_8X8_GLYPHS`, ASCII 0x20–0x7F, eight bytes a glyph, most significant bit leftmost; a header comment stating the provenance (authored here, or a set published as public domain with its source named; anything under a licence needs owner approval first, R14's spirit).
- `NeuronClient/Shaders/GlyphVS.hlsl`, `GlyphPS.hlsl`: quads with integer texel coordinates; `Texture2D<uint>` and `Load`; `discard` on zero.
- `NeuronClient/GlyphPipeline.h` + `.cpp`: root signature with a descriptor table (one SRV) and root constants; one PSO from `PipelineDefaults`; a shader-visible CBV/SRV/UAV heap of a few slots owned here.
- `NeuronClient/TextRenderer.h` + `.cpp`: uploads the atlas (128×48 texels, `R8_UINT`, 16 glyphs a row) once; `Draw(x, y, std::string_view, colorRgba, scale = GLYPH_SCALE)`, `Measure(text, scale)`, `GLYPH_SCALE = 3`, `GLYPH_WIDTH_PIXELS = 8` (scale 3 because the screen is 1920×1080: a 24-pixel cell divides it exactly, into the same 80×45 grid the 16-pixel cell gave at 1280×720); unknown characters draw the 0x7F glyph.
- `NeuronClientTests/TextRendererTests.cpp`: on WARP, draw `"A"` at (0, 0) scale 1 and read back the eight rows against the glyph's bits.

## Acceptance criteria

- [x] Every printable ASCII character is legible on screen at `GLYPH_SCALE` 3, the screen's scale; the report says the full set was displayed and looked at. ASCII 0x20-0x7E is the whole set the client can draw, so the whole set is the test (UI §6).
- [x] The glyph path uses `Texture2D<uint>::Load()` and no sampler — not because a sampler is disallowed (it is not, since 2026-09-16) but because filtering a glyph at a non-integer position is blur, which is what R12's 1:1 screen exists to prevent.
- [x] The readback test matches the glyph bits exactly.
- [x] The atlas upload happens once, through an upload heap and `CopyTextureRegion` with the 256-byte-aligned row pitch, and is released after the copy fences.
- [x] `Measure` agrees with what `Draw` covers, so NC-025 can lay text out.

## Verification

```powershell
x64\Debug\NomadCommander.exe        # the character set and a sample sentence at scale 1 and 3
vstest.console.exe x64\Debug\NeuronClientTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

Kerning, proportional widths, UTF-8 beyond ASCII (names in the GDD are ASCII), a second font size other than integer scales, text effects.

## Notes

- `discard` is not blending: the pixel is not written. That is why panels can be opaque and text can sit on them without an alpha channel, and it stays the cheaper path now that blending is available.
- The atlas is `R8_UINT`, one texel a pixel, decoded from the bit array at upload; 6 KiB of texture for 768 bytes of data is a fine trade for a `Load` per pixel.

## Report

**Built, run and read off the screen.** `x64\Debug\NomadCommander.exe` draws the whole printable set twice — at `GLYPH_SCALE` 3 and again at scale 1 — plus a title and two sentences of the kind the desk will carry.

**The font is authored here, and that is stated in the header.** It is not derived from any published font, so there is no licence to honour and no attribution owed. The shapes were designed as pictures and the bytes derived from them, which is why `BitmapFont.h` holds binary literals rather than hex: a reader can see the letter, and an editor can change it by moving a `1`. Six columns wide of the eight so a character carries its own spacing, rows 0–6 for capitals, row 7 for the line below and for the descenders of `g j p q y`.

**The acceptance criterion says the full set must be displayed and looked at, so it was — and read back rather than glanced at.** With the game running, the three scale-1 rows were captured from the screen and printed as their pixels. All 95 printable characters are legible: `` !"#$%&'()*+,-./0123456789:;<=>?`` , `@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\]^_` and `` `abcdefghijklmnopqrstuvwxyz{|}~ ``, with the underscore's row-7 descender showing where it should. One glyph was moved on the strength of looking at it: `~` sat on rows 0–1, which read as an accent rather than a tilde, and is now on rows 2–3.

**The readback test goes further than the task asked.** It wanted `"A"` at (0,0) scale 1 checked against the glyph's bits; that test exists, and so does `EveryPrintableCharacterMatchesItsGlyphBits`, which draws all 96 and compares **every one, bit for bit**. A single letter would not catch an atlas laid out wrong, an off-by-one in the codepoint index, or a row pitch mishandled on upload; all 96 do.

**`ScalingIsExactlyIntegerSoATexelIsASquareBlock` is the test that defends §5's point.** At scale 3 every texel must be a 3×3 block of identical pixels, checked pixel by pixel across the whole glyph — no blended edge, no half-lit pixel anywhere. That property is what `Texture2D<uint>::Load()` buys and what a sampler would take away. There is no sampler in the glyph path and no static sampler in its root signature, so there is nothing to switch on by accident.

**Refined against the code as it is.**

- **`GlyphPipeline` owns the descriptor heap, `TextRenderer` owns the atlas.** Eight slots rather than one, so a second thing wanting a texture in this pass has somewhere to put it without a second heap. The atlas view goes in slot 0.
- **The upload buffer is a local.** It is created, mapped, filled from the bit array, copied with `CopyTextureRegion` at the 256-byte-aligned row pitch the SDK reports, and released on the way out of `Create` once the copy has fenced — which is the whole of its life. The atlas ends in `PIXEL_SHADER_RESOURCE` and is never written again.
- **The texel coordinate interpolates; the colour does not.** That pairing is what makes the scaling exact: at scale 3 the three screen pixels across a texel land at texel + ⅙, + ½ and + ⅚, and all three truncate to the same texel. The colour is a bit pattern and is `nointerpolation`, for the same reason it is in the primitive pass.
- **`Measure` and `Draw` agree by construction** — both are the character count times the cell, and neither kerns — and a test pins it anyway, because NC-025 lays panels out with one and fills them with the other.
- **An unknown character draws 0x7F**, a filled box, so a gap in the data is loud on screen rather than an invisible hole. Tested.
- **`TextRenderer` has its own vertex ring** rather than sharing `PrimitiveBatch`'s: the vertex formats differ (a glyph carries a texel coordinate), so sharing would mean a union or a template for a hundred lines of buffer management. Worth revisiting if a third pass wants one; two is not yet a pattern.

**clang-tidy found four things and none were suppressed:** three `bugprone-implicit-widening-of-multiplication-result` where a byte count was multiplied in 32 bits and then used as a 64-bit index — including the font array's own bound, which is now a `std::size_t FONT_TOTAL_BYTES` — and one integer division used in a floating-point context in `AtlasOrigin`, where the row and column are genuinely integral and are now computed as integers before being converted once.

**Verified:** `CheckFormat.py` (82 files), `CheckProjectFiles.py` (9 projects, clean), `RunClangTidy.py` (**37 translation units clean**). Debug **and** Release rebuild with zero warnings. All four suites: **104 of 104 green**, 26 of them in `NeuronClientTests`, 5 new here. The debug layer said nothing across the desktop run.

**Not done, and worth naming.** ADR-009 still owes its photograph — the same text at 1:1, at 2× point and at a fractional bilinear present scale. **This task is the first that could take it**, because there is finally text to photograph, but this machine's monitor is 1920×1080, so the present step takes its 1:1 copy path and the other two cases cannot be produced here. What can now be said is narrower and worth having: at the present scale of 1, an 8×8 glyph at `GLYPH_SCALE` 3 reaches the glass as exact 3×3 blocks, measured. The comparison ADR-009 wants needs a second monitor or a windowed mode, and ADR-010 removed the second of those on purpose.
