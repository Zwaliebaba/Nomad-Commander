# NC-028 — The desk's text faces

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient, Tools | L | **yes** | **yes** | Done (PR #4) |

**Depends on:** NC-023, NC-025, NC-026
**Read first:** AGENTS.md R13 (what may be compiled in and what may not be loaded), R14 (the closed dependency list, and what "third party" means for content), §5 (*A sampler on text costs the 1:1 guarantee*; blending is a pass's own business); `Design/UI/UI-Spec.md` §1, §2, §6; ADR-008, ADR-009, ADR-012

## Goal

Replace the 8×8 bitmap font with three baked faces of IBM Plex Mono, so that the desk's type has one baseline, the density the reference screens were drawn at, and a shape somebody designed. The cell grid, the screen and every widget are unchanged; what changes is that a character is half a cell wide instead of a whole one, and that a glyph carries coverage rather than a bit.

**This task exists because the font was measured, not because it was disliked.** NC-023's font has two baselines — capitals and digits end on row 5, lowercase on row 6 — and its tests could not see it, because they compare the screen against the same bytes. And its 24-pixel advance is twice the 12 the `Design/UI` screens were authored at, which makes *the owner recognises the screen row for row* unmeetable in NC-072, NC-073 and NC-076. Both are cheapest to fix now, while no screen draws real text.

## Deliverables

- `Tools/BakeFont.py`: rasterizes the faces through Pillow and FreeType into two committed headers, with the sources' SHA-256, the font's version, the rasterizer versions and the licence text written into the output. A development tool that never ships and that CI never runs (A8).
- `NeuronClient/FontData.h`, `NeuronClient/FontCoverage.h`: generated. The metrics and the 78,336 coverage bytes, split so that only `Font.cpp` pays to parse the bytes.
- `NeuronClient/Font.h` + `.cpp`: `enum class Font { Body, Small, Title }` and `MetricsOf`, which is the whole of what the rest of the client knows about how a glyph is stored.
- `NeuronClient/TextRenderer.*`: one atlas of the three faces and the icons; `Draw` and `Measure` take a `Font` where they took a scale; `DrawIcon` keeps the cell.
- `NeuronClient/Shaders/GlyphPS.hlsl`: `Texture2D<float>`, coverage as alpha. `GlyphPipeline.cpp`: straight-alpha blending, the one field it overrides.
- `NeuronClient/Ui.*`: `Label` takes a `Font`; `Panel` titles in `Title`.
- **Deleted:** `NeuronClient/BitmapFont.h`.
- `NeuronClientTests/TextRendererTests.cpp`: rewritten against coverage.

## Acceptance criteria

- [x] Every glyph of every face matches its baked coverage, blended over the background, read back from a real draw.
- [x] An anti-aliased edge blends per channel toward what is under it — the property bits could not have.
- [x] An icon is still its 8×8 art, three texels a bit, with no blended edge anywhere.
- [x] A line of text is exactly one cell tall in every face, asserted at compile time, so `Rect::Cell` and every widget are untouched.
- [x] `Measure` agrees with what `Draw` covers, in a face whose advance is not half a cell.
- [x] The executable ships alone (R13): no `.ttf`, no Pillow, no tool at build time. CI runs unchanged.
- [x] The licence travels with the bytes, and the artifact is not called Plex.
- [x] Run on a desktop: the three faces draw the whole printable set and the desk reads.

## Verification

```powershell
py Tools\BakeFont.py --regular IBMPlexMono-Regular.ttf --semibold IBMPlexMono-SemiBold.ttf --license OFL.txt
msbuild NomadCommander.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo
vstest.console.exe x64\Debug\NeuronClientTests.dll /Platform:x64
x64\Debug\NomadCommander.exe        # three faces, the whole printable set in each
```

## Decisions to record

**Written: [ADR-016](../../Design/ADR/ADR-016-the-desk-text-faces.md) — the desk's text faces** (owner-visible). Three faces named by role, baked to the cell row, coverage rather than bits, blending in the glyph pass alone, the OFL text travelling with the artifact. Its *What this forecloses* is the part worth reading: a glyph can no longer be edited by moving a `1`, and the output is only as reproducible as the rasterizer that made it.

## Out of scope

Kerning, proportional widths, a fourth face, hinting options, subpixel positioning, UTF-8 beyond ASCII, text effects, and any layout change — the cell grid does not move in this task.

## Notes

- The advance is why the sizes are what they are: IBM Plex Mono advances 600/1000 em, so 20 px gives exactly 12 and 16⅔ gives exactly 10. A size that gave a fractional advance would put a character at a non-integer position, which UI §1 forbids.
- `discard` is kept for a texel with no coverage. It is cheaper than blending at zero and it is what lets an opaque panel sit under text unchanged.

## Report

**Owner-visible, and the decision is [ADR-016](../../Design/ADR/ADR-016-the-desk-text-faces.md).** It was landed with the work rather than on its own, as NC-025 and NC-027 were, and for the same reason: the owner asked for the change in the same sitting. The ADR is a separate file so it can be read and reversed on its own, and the revert is genuinely small — four files and a restored header.

**The defect was measured before anything was replaced.** Parsing `BitmapFont.h`'s own binary literals and taking each glyph's last non-zero row: all 26 capitals and all 10 digits end on row 5, every lowercase letter without a descender on row 6, and `g p q y` on row 7. So a word with both stepped 3 px at the baseline at `GLYPH_SCALE` 3. The punctuation split the same way. **NC-023's tests were green over this**, because `EveryPrintableCharacterMatchesItsGlyphBits` compared the screen against the same bytes the defect was in — a test can only check that the renderer drew what it was given, and what it was given was wrong. That is worth remembering the next time a readback test is called thorough.

**The second measurement is the one that decided it.** The `Design/UI` screens were authored in a browser at IBM Plex Mono 13–14 px scaled 1.5×, which is a 12-pixel advance; the bitmap font advanced 24. The side panel of `screens/02` holds 46 characters a line where the engine could draw 22. Three Phase 5 tasks carry *the owner recognises the screen row for row*, and that criterion could not have been met.

**Built, run and photographed.** Debug **and** Release rebuild with zero warnings. The executable was run on a 1920×1080 monitor and captured at physical resolution — the first capture came back 1536×864 because the capturing process was not DPI aware, which is worth knowing before anybody reports the client as blurry; the game is per-monitor-v2 aware from NC-001's manifest and was drawing 1:1. The three faces each draw the whole printable set, the icons are unchanged, and `One tap behind` now fits the button that used to clip it to `One tap behi`.

**Refined against the code as it is.**

- **Two generated headers, not one.** The coverage is 78,336 bytes of initializer and MSVC pays for it in every translation unit that sees it, so the metrics live in `FontData.h` (2 KB, included freely) and the bytes in `FontCoverage.h`, included by `Font.cpp` alone.
- **Faces are named for their role, not their size.** `Body`, `Small`, `Title` rather than `Mono20` or `Bold`. A screen says what a line is; re-baking `Title` at another weight then touches no call site.
- **The generator emits the line break clang-format would have made**, so re-running the tool is a no-op against `CheckFormat.py` rather than a two-step dance.
- **The icons were nearly a casualty.** Scaling 8×8 art to a 24-pixel cell through coverage would have blurred it; instead a set bit becomes a 3×3 block of full coverage, and a test asserts every one of those nine pixels, so an icon reaches the glass as exactly the pixel art it was drawn as.
- **`Font::Body` is a default argument on `Draw` and `Label`**, which is what kept the diff in `Ui.cpp` to the six call sites that wanted something else rather than every call site in the tree.

**The tests are stronger than the ones they replace.** `EveryGlyphOfEveryFaceMatchesItsCoverage` reads back all 96 glyphs of all three faces and checks every pixel per channel against the baked byte blended over the background. `PartialCoverageBlendsTheInkTowardWhatIsUnderIt` draws `g` in a colour whose three channels differ, so a blend that mixed the channels or applied coverage to the wrong one fails; that property did not exist when a glyph was a bit. A full or empty texel must match exactly and a partial one may differ by one level, which is the rounding a blend unit is allowed — stated as a tolerance rather than discovered as a flake.

**clang-tidy found one**, and it was real in the way this check usually is: two multiplications in `TextRenderer.cpp`'s icon upload were done in 32 bits and then widened to index a buffer. They are `std::size_t` before the multiply now.

**Verified:** `CheckFormat.py` (115 files), `CheckProjectFiles.py` (9 projects, clean), `RunClangTidy.py` (**54 translation units clean**, on 22.1.3 against CI's pinned 22.1.8 — the script warns about the gap and it is worth reading if CI disagrees). All four suites: **174 of 174 green**, 75 in `NeuronClientTests`. The debug layer said nothing across the desktop run.

**Noticed and left alone.** `NomadCommander/Main.cpp` is still every task's demo stacked into one frame — NC-022's test pattern under NC-026's widgets under NC-027's map — with the input readout drawn at fixed coordinates over the widget panel, so two demo labels overlap. It is the accumulated Phase 1 proof-of-work and NC-070's composition root replaces the whole of it; this task changed only the lines that named a scale. The scene target also still clears to the provisional slate of NC-021 rather than `Palette::BACKGROUND`, which is why the map's lanes are nearly invisible against it.

**Not done, and named.** ADR-009 still owes its photograph of text at 1:1 against a scaled present. This task could not take it either: the monitor here is 1920×1080, so the present step takes its copy path. What can be said is narrower and now matters more — anti-aliased type survives a non-integer present scale far better than bitmap type does, so ADR-009's named cost is smaller after this change than before it. Somebody with a second monitor should still take the picture.
