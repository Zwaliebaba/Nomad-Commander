# NC-029 — The present scale, measured

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient, Tests/NeuronClientTests | M | **yes** | no | Done (ab488a9) |

**Depends on:** NC-021, NC-028
**Read first:** [ADR-009](../../Design/ADR/ADR-009-the-scene-target-and-the-present-scale.md) whole, and its *Measurements* section twice; [ADR-010](../../Design/ADR/ADR-010-the-borderless-window.md); [ADR-016](../../Design/ADR/ADR-016-the-desk-text-faces.md); AGENTS.md R12, §5 (*A sampler on text costs the 1:1 guarantee*), §6 (*Figures in an ADR are measured, not estimated*); the reports of NC-021, NC-027 and NC-028, each of which names this debt unpaid

## Goal

ADR-009 ends with an obligation nobody has discharged: *"NC-021 is to photograph the same text at 1:1, at 2× point and at 0.71× bilinear and put the three in its report, because 'softer text' is the kind of claim that should be looked at rather than argued about."* Its *Measurements* section still reads **None quoted** — the only ADR in the register that quotes no figure while making a claim about what a player sees. This task pays that debt with a readback measurement instead of a photograph, and amends ADR-009 with the numbers.

**Added after the fact** (`Plan/README.md`, *Adding, splitting and dropping tasks*), motivated by three reports rather than a plan: NC-021's *"ADR-009 owes a photograph… this task does not discharge it"*, NC-027's *"ADR-009's photograph is still unpaid"*, and NC-028's *"Somebody with a second monitor should still take the picture."*

## Why it was stuck, and why it is not

Every report read the obligation as a *photograph*, and a photograph of a scaled present needs a monitor that is not 1920×1080. That reading is what kept it open across three tasks on a machine that has one 1080p display.

The numbers never needed the monitor. **A back buffer's size comes from `DXGI_SWAP_CHAIN_DESC1`, not from the display**, and NC-021 already stands up a real swap chain on a window it creates and never shows (`TwoHundredFramesPresentWithoutTheDebugLayerComplaining`). So all three of ADR-009's filter cases are reachable on this machine, in `NeuronClientTests`, on WARP — which also makes the figures reproducible on the CI runner rather than dependent on whose desk they were taken at (A12).

The two halves of the obligation are therefore different debts, and this task is honest about paying only one:

- **The measurement** — what the present scale does to the pixels — is mechanical, monitor-independent, and this task's whole content.
- **The judgement** — whether the result is acceptable to look at — is the owner's, still wants eyes on a non-1080p display, and stays open after this lands. `PresentPass::Fit` already has the arithmetic tested over roughly ten thousand client sizes; what nobody has is a number for what the sampler does to a glyph.

## Deliverables

- `Tests/NeuronClientTests/PresentScaleTests.cpp`, registered in the `.vcxproj` **and** the `.filters`: one known string drawn once into the scene target through NC-028's faces, then presented through `PresentPass::Execute` into back buffers of **1920×1080** (`Filter::None`), **3840×2160** (`Filter::Point`) and **1366×768** (`Filter::Linear`), each read back and compared against the scene target it came from.
- Per case, figures fit to print in an ADR: the `Placement` chosen, the count of ink pixels whose value moved, the largest single-channel move, and the count of colours present in the result that were not present in the scene target.
- **ADR-009's *Measurements* section amended in the same commit** (AGENTS.md §6): the three figures, and the method that produced each, replacing *None quoted*.
- Whatever minimum change `PresentPass` or `SwapChainTarget` needs to be driven at a size the monitor does not have — named in the report, not assumed here.

## Acceptance criteria

- [ ] All three cases are measured from **one** scene target and **one** string, in `NeuronClientTests` on WARP, so CI reproduces the figures (A12).
- [ ] `Filter::None` moves no pixel: the back buffer's 1920×1080 region equals the scene target byte for byte. This is the one claim ADR-009 makes that can be proved rather than argued, and it is the path every 1080p player takes (ADR-010).
- [ ] `Filter::Point` introduces **no colour that was not already in the scene target** — the claim `PresentPass.h` makes in its own comment. Since ADR-016 a glyph reaches the scene target already blended against its background, so this is a check on the sampler and not on the font.
- [ ] `Filter::Linear` is quantified rather than described: ADR-009's *"softer text"* comes back as a number a reader can disagree with.
- [ ] ADR-009's *Measurements* section no longer reads *None quoted*, and each figure says how it was taken.
- [ ] Nothing about the filter policy, the scene target's size, any pipeline or any shader changed. **This task measures; it does not decide.**

## Verification

```powershell
msbuild NomadCommander.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo
vstest.console.exe x64\Debug\NeuronClientTests.dll /Platform:x64
py Build\CheckProjectFiles.py
py Build\CheckFormat.py
py Build\RunClangTidy.py
```

**Desktop run: no**, and that is the point of the task rather than a gap in it. What a desktop run would add — the picture on a non-1080p display — is the half this task does not claim.

## Decisions to record

**None.** No new ADR: this amends [ADR-009](../../Design/ADR/ADR-009-the-scene-target-and-the-present-scale.md)'s *Measurements* section, in the same commit as the test that produced the figures.

## Out of scope

Changing which filter any case gets. MSAA and supersampling — [ADR-013](../../Design/ADR/ADR-013-anti-aliasing-the-3d-map.md) owns those and its trigger is NC-072. A camera. ADR-010's open comparison between filling the client area and using the largest integer scale with bars: that is a policy question for the owner, and this task's figures are an input to it, not an answer.

## Notes

- **Do not invent a test-only target.** A12's lesson is `FrameTarget`: the ADR-009 scene target is the game's own, and the suite reads the pixels a player would see rather than a parallel object. If `PresentPass::Execute` has to widen to be driven at an arbitrary size, widen it for the game and say so; do not add a type only the tests construct.
- ADR-009's *What this forecloses* paragraph is written about 8×8 bitmap text — *"resampling an 8×8 glyph by a non-integer factor is the one thing bitmap type is worst at"* — and ADR-016 replaced that font with anti-aliased coverage. NC-028's report already argues the cost is smaller than that paragraph states. **Amending the paragraph is the owner's call, not this task's**; supply the number and say in the report that the paragraph now overstates its own cost.
- NC-027's report is the argument for this shape of task in one line: *"A screenshot would have shown a map."* A readback caught a matrix-convention bug that looked entirely plausible on screen.

## Report

**The debt was not hardware-blocked, and that is the finding.** NC-021, NC-027 and NC-028 each recorded ADR-009’s figures as owed and each named the same cause — one 1920×1080 monitor. All three read the obligation as a *photograph*, and a photograph of a scaled present really does need a display that is not 1920×1080. The numbers never did: a destination’s extent is a parameter, not a property of the monitor. Once that was seen the task was an afternoon.

**The three figures ADR-009 asked for, and its *Measurements* section no longer reads "None quoted".**

| | 1:1 — 1920×1080 | 2× point — 3840×2160 | 0.71× bilinear — 1366×768 |
|---|---|---|---|
| Pixels in the placement | 2,073,600 | 8,294,400 | 1,048,320 |
| Pixels moved | **0** | **0** | 14,480 |
| Largest single-channel move | 0 | 0 | **151** of 255 |
| Colours the source does not contain | 0 | 0 | 6,530 |
| Lit / partial | 21,538 / 15,905 | 86,152 / 63,620 | 15,056 / 14,669 |
| Partial as a share of lit | 73 % | 73 % | **97 %** |

**The 2× column is exact rather than close.** 86,152 is exactly 4 × 21,538 and 63,620 exactly 4 × 15,905 — every texel became precisely its own 2×2 block. That is a stronger statement than the criterion asked for (it wanted "no colour the source does not contain", which follows from it), and it is the kind of result that is either exact or wrong, so it is worth having as an equality.

**The headline cost is 97 %.** After a 0.71× bilinear present only 3 % of the lit pixels are still full-strength ink where 27 % were before, and a single channel can move 151 of 255. ADR-009 guessed "softer text" and the guess was right; this is how soft.

**Refined against the code as it is.**

- **The one engine change the task anticipated was needed, and it is eleven lines.** `PresentPass::Execute` took a `SwapChainTarget` and nothing else, so the destination size was whatever the window happened to be. It now has an overload taking the destination resource and its extent, and the swap-chain one is written in terms of it — which is also the more honest decomposition, because this pass never needed a swap chain, only somewhere to put pixels and a size to fit against.
- **No new type, and the destination is a `SceneTarget`.** A12’s lesson is `FrameTarget`: the suite reads the pixels a player would see rather than a parallel object. `SceneTarget::Desc` already carried width and height, so the destination needed no change at all — the thing that made all three cases reachable on one monitor was already in the tree and nobody had noticed.
- **The sample is every glyph of every face, not a sentence.** A number about "text" should not be a number about the letters one sentence happened to use; a face’s widest stem and its thinnest diagonal resample differently and both are on screen. The prose line is there for ordinary spacing.
- **The softness metric is deliberately ADR-013’s methodology.** Partial-over-lit is a coverage measure of the same shape as "655 of 655 edge pixels have exactly the background immediately outside", so the two ADRs can be read against each other rather than each in its own units.
- **Desktop run moved from *no* to *yes*.** The task argued the run added nothing, and for the *figures* that is still true. But this changed the code path the game presents through on every frame, and AGENTS.md §7 requires a run for anything touching presentation — which outranks a task file. It was run.

**`CheckProjectFiles.py` caught an R11 violation nobody would have caught by eye**: a test method named `...InventsNoColour`. Prose may spell it either way and this tree’s identifiers may not. That check earns its place on names like this one, where the word is buried in the middle of a forty-character method name.

**Verified:** `CheckFormat.py` (115 files), `CheckProjectFiles.py` (9 projects, clean), `RunClangTidy.py` (**54 translation units clean**, on 22.1.3 against CI’s pinned 22.1.8). Debug **and** Release rebuild with zero warnings — Release because this changed engine code on the frame path and CI does not build it (§6). All four suites: **176 of 176 green**, 78 in `NeuronClientTests`, 3 new here. The debug layer said nothing across the run.

**Run, and what that did and did not establish.** `x64\Debug\NomadCommander.exe` was launched, created its borderless 1920×1080 window, ran four seconds and exited with code 0 on Escape — so the widened `Execute` works against the real swap chain and not only against a `SceneTarget`. **The frame was not photographed.** Two attempts to capture it from a background shell came back with the desktop rather than the game, and the second also reproduced NC-028’s DPI trap (1536×864 until the capturing thread is made per-monitor aware). Both captures were deleted. This is a smaller loss than it sounds: the readback tests compare 11.4 million pixels against their sources, which is the point NC-027 made when a screenshot would have shown a perfectly plausible map with a broken depth test.

**Assumed:** that WARP’s bilinear and point samplers behave as a discrete GPU’s do. The 1:1 and 2× columns cannot depend on it — a copy and an exact block mapping have nothing to round — but the 0.71× column is a filter’s arithmetic, and another adapter could land a channel a level or two elsewhere. The shape of the finding would not move; the last digit might.

**Bent:** nothing.

**Noticed and left alone.**

- **ADR-010 says displays larger than the screen were "knowingly made worse", and at exactly 3840×2160 that is now measurably false** — the present is lossless there, four pixels a texel, nothing invented. 4K is a common desktop. That is an argument about ADR-010’s wording, the owner’s to make, and this task did not touch it.
- **ADR-009’s *What this forecloses* paragraph is written about the 8×8 bitmap font ADR-016 replaced**, so "the one thing bitmap type is worst at" overstates the cost as the client now stands. Flagged inside the ADR’s *Measurements* section, where a reader of the figures will meet it; the paragraph itself is the owner’s to rewrite.
- **The judgement half of the obligation is still open**, and no amount of readback closes it: nobody has seen the 0.71× case on a real 1366×768 panel. What changed is that the argument now has numbers on both sides of it.
