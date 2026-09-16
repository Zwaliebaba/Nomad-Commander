# NC-023 — The bitmap font and text

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient | M | **yes** | no | Open |

**Depends on:** NC-022
**Read first:** AGENTS.md R13 ("a bitmap font is 96 glyphs, 8×8, one bit a pixel, 768 bytes, and nothing to load"), §5 (*A bitmap font is read with `Texture2D<uint>::Load()`*), R14

## Goal

Text on screen from data compiled into the executable: the 96 printable ASCII glyphs as a `constexpr` array, uploaded once to an integer texture, drawn as quads by a second pipeline whose pixel shader loads texels and discards the zeros. Integer scaling only, so text stays crisp at 1:1.

## Deliverables

- `NeuronClient/BitmapFont.h`: `inline constexpr std::array<std::uint8_t, 768> FONT_8X8_GLYPHS`, ASCII 0x20–0x7F, eight bytes a glyph, most significant bit leftmost; a header comment stating the provenance (authored here, or a set published as public domain with its source named; anything under a licence needs owner approval first, R14's spirit).
- `NeuronClient/Shaders/GlyphVS.hlsl`, `GlyphPS.hlsl`: quads with integer texel coordinates; `Texture2D<uint>` and `Load`; `discard` on zero.
- `NeuronClient/GlyphPipeline.h` + `.cpp`: root signature with a descriptor table (one SRV) and root constants; one PSO from `PipelineDefaults`; a shader-visible CBV/SRV/UAV heap of a few slots owned here.
- `NeuronClient/TextRenderer.h` + `.cpp`: uploads the atlas (128×48 texels, `R8_UINT`, 16 glyphs a row) once; `Draw(x, y, std::string_view, colorRgba, scale = GLYPH_SCALE)`, `Measure(text, scale)`, `GLYPH_SCALE = 2`, `GLYPH_WIDTH_PIXELS = 8`; unknown characters draw the 0x7F glyph.
- `NeuronClientTests/TextRendererTests.cpp`: on WARP, draw `"A"` at (0, 0) scale 1 and read back the eight rows against the glyph's bits.

## Acceptance criteria

- [ ] Every printable ASCII character is legible on screen at scale 2; the report says the full set was displayed and looked at.
- [ ] No sampler exists in the root signature or the shader (AGENTS.md §5).
- [ ] The readback test matches the glyph bits exactly.
- [ ] The atlas upload happens once, through an upload heap and `CopyTextureRegion` with the 256-byte-aligned row pitch, and is released after the copy fences.
- [ ] `Measure` agrees with what `Draw` covers, so NC-025 can lay text out.

## Verification

```powershell
x64\Debug\NomadCommander.exe        # the character set and a sample sentence at scale 2 and 3
vstest.console.exe x64\Debug\NeuronClientTests.dll /Platform:x64
```

## Decisions to record

None.

## Out of scope

Kerning, proportional widths, UTF-8 beyond ASCII (names in the GDD are ASCII), a second font size other than integer scales, text effects.

## Notes

- `discard` is not blending: the pixel is not written. That is why panels can be opaque and text can sit on them without an alpha channel.
- The atlas is `R8_UINT`, one texel a pixel, decoded from the bit array at upload; 6 KiB of texture for 768 bytes of data is a fine trade for a `Load` per pixel.

## Report

_Filled in on hand-back._
