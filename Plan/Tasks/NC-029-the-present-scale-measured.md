# NC-029 — The present scale, measured

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient, Tests/NeuronClientTests | M | no | no | Open |

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

_Filled in on hand-back._
