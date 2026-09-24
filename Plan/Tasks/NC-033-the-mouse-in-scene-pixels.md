# NC-033 — The mouse in scene pixels

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient, NomadCommander, Tests/NeuronClientTests | S | **yes** | no | Open |

**Depends on:** NC-021, NC-024
**Read first:** [ADR-009](../../Design/ADR/ADR-009-the-scene-target-and-the-present-scale.md) whole, and its first decision twice (*"exactly one place knows, and it knows only how to fit one rectangle inside another"*); [ADR-010](../../Design/ADR/ADR-010-the-borderless-window.md) (the client area is the monitor's, so on anything but a 1920×1080 monitor the present step scales); [ADR-012](../../Design/ADR/ADR-012-the-ui-model.md) (`Ui` hit-tests against `InputState`); AGENTS.md R12, §3 (*Report what you actually did*), §7; the reports of NC-024 and NC-029

## Goal

The mouse reports where it is **in the 1920×1080 scene**, not in the window's client area, so the pointer, every widget's hit test and every Phase 5 screen agree with what the glass shows on any display. ADR-009 put a transform between the scene and the glass — scaled to fit, centred, barred — and applied it on the way out; nothing applied its inverse on the way in. The present step already decides where the scene goes (`PresentPass::Fit`); this task gives it the way back and takes every mouse message through it.

**Added after the fact** (`Plan/README.md`, *Adding, splitting and dropping tasks*): a defect the owner found on 2026-09-24, on the first desktop run on a display that is not 1920×1080 — a 3:2 panel, letterboxed top and bottom. NC-024's crosshair sat about 3 cm below and to the right of the Windows pointer. The screenshot settles the mechanism without any guessing: the cross is drawn at scene (710, 418) to the pixel, which is the raw client coordinate the readout printed, so the scene's scale and letterbox offset were applied to it once more on the way to the glass.

**It is not cosmetic.** `Ui` hit-tests with the same point (`Ui.cpp`, three call sites), so clicks land under the cross rather than under the pointer. Below 1920×1080 — the case ADR-009's scale exists for — the raw point never grows large enough to reach the right and bottom of the scene, so part of the desk cannot be clicked at all.

**Why every earlier run missed it.** NC-024 measured the mouse on a 1920×1080 monitor, which takes ADR-009's 1:1 copy path, where a client pixel *is* a scene pixel and the missing inverse is invisible. NC-025 clicked its button on the same machine. NC-029 measured the present scale by reading back pixels, with no mouse involved. ADR-009 says nothing about input. The owner's panel is the first time a mouse met the scaled path.

## Deliverables

- `NeuronClient/PresentPass.h` + `.cpp`: `static std::int32_t ScenePixelUnder(clientPixel, placementOrigin, placementExtent, sceneExtent)` beside `Fit` — the way back along one axis, measured at the client pixel's centre and rounded towards negative infinity. It lives with `Fit` so that the one place that knows how the scene was fitted also knows how to take a point back through it (ADR-009's first decision).
- `NeuronClient/InputState.h` + `.cpp`: `SetScenePlacement(const PresentPass::Placement&, sceneWidth, sceneHeight)`; every mouse message's point is taken through `ScenePixelUnder` before it is stored, so `MousePosition()` and `MouseDelta()` are in scene pixels. An unplaced scene is ADR-009's 1:1 case and passes points through untouched.
- `NomadCommander/Main.cpp`: one call, after the swap chain exists, handing the mouse `Fit` over the same four sizes `PresentPass::Execute` uses every frame.
- `Tests/NeuronClientTests/InputStateTests.cpp`: the defect's regression on a 3:2 panel, the whole desk reachable below 1920×1080, the identity at 1920×1080, movement in scene pixels, a captured drag outside the scene landing outside it, and every client pixel of twelve real display shapes landing on the scene pixel whose footprint holds its centre.
- `Tests/NeuronClientTests/GraphicsDeviceTests.cpp`: `ScenePixelUnder` swept over the same client areas `Fit`'s own property test sweeps, beside it.
- ADR-009's *Consequences* gains the input bullet; `Plan/Roadmap.md` gains this row, and NC-070 — which moves the frame loop into `App.cpp` and must carry the call with it — depends on this task.

## Acceptance criteria

- [ ] On a display that is not 1920×1080 the crosshair sits under the Windows pointer in all four corners of the picture, and the readout says `0,0` at its top left and `1919,1079` at its bottom right (desktop run, R12).
- [ ] At 1920×1080 nothing changes: the way back is the identity for every coordinate, negative ones under capture included, so NC-024's measured `(800, 400)` still reads `(800, 400)`.
- [ ] Every client pixel inside the placement lands on the scene pixel whose footprint holds its centre — the texel a point sampler reads there, which is the rule `PresentScaleTests` measures the present step against — and every pixel in a bar lands outside the scene.
- [ ] The whole desk is reachable at every real display size: the placement's corner pixels land on the scene's corner pixels, including below 1920×1080, where before this task the right and bottom of the desk could not be clicked.
- [ ] A point left of or above the scene lands at a negative coordinate and never on the scene's first column or row: rounding is towards negative infinity, not towards zero.
- [ ] Nothing but the present step decides where the scene is. `InputState` is handed `Fit`'s answer and neither it nor `Ui` asks the window how big it is (ADR-009, R12).
- [ ] `Build/CheckFormat.py`, `Build/CheckProjectFiles.py` and `Build/RunClangTidy.py` are clean; the four suites pass.

## Verification

```powershell
msbuild NomadCommander.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo
vstest.console.exe x64\Debug\NeuronCoreTests.dll x64\Debug\NeuronClientTests.dll `
                   x64\Debug\NeuronServerTests.dll x64\Debug\GameLogicTests.dll /Platform:x64
python Build\CheckFormat.py
python Build\CheckProjectFiles.py
python Build\RunClangTidy.py
x64\Debug\NomadCommander.exe
```

**Desktop run: yes, on a display that is not 1920×1080** — the defect cannot be seen on one that is. Move the pointer to each corner of the picture (not of the screen: on a letterboxed panel the picture stops above and below the screen's edges). The cross is under the pointer at each, and the readout reads `0,0` top left and `1919,1079` bottom right. Then click the demo's buttons and the scrollbar with the pointer, not the cross.

## Decisions to record

**None.** ADR-009 decided the transform and this task applies its inverse; nothing is decided that the ADR did not already decide. Its *Consequences* gains one bullet saying where input is taken back through it, because a reader of that ADR is who needs to know — the decision itself is unchanged.

## Out of scope

Hiding the Windows pointer or replacing NC-024's crosshair with a cursor of the game's own. A resize or `WM_DPICHANGED` path: the window is fixed (ADR-010), so the placement is set once, and a window that could change size would have to set it again. Raw input. Anything about how the picture looks at a fractional scale — that is ADR-009's cost, measured by NC-029, and the owner's judgement.

## Notes

- **Why at the pixel's centre.** The point sampler reads the texel under a pixel's centre, and `PresentScaleTests`' `NearestSourceIndex` measures the present step against exactly that. Taking the mouse back by the same rule means the pointer answers to the texel the glass shows under it: the identity at 1:1, a plain division at an exact multiple, and whichever texel's footprint holds the centre in between.
- **Why in `InputState`, and not in `Ui` or the screens.** Every reader of the mouse wants scene pixels and none wants client pixels. Converting where the message arrives means nothing downstream can forget to, which is the property the defect lacked.
- **Why rounding towards negative infinity matters.** C++ rounds a quotient towards zero, which would fold the first pixel outside the scene — in a bar, or past the window under capture — onto the first pixel inside it, and a widget on the edge would answer to a pointer that is not on it.
- If the owner's panel runs above 125 % scaling, the desktop run also covers the gap NC-024's report named: its criterion asked for 150 % and it was measured at 125 %.

## Report

**What is built.** `PresentPass::ScenePixelUnder` sits beside `Fit` and takes one axis of a client pixel back to the scene pixel it shows. `InputState::SetScenePlacement` hands the mouse `Fit`'s answer, and every mouse message's point goes through it before anything is stored, so `MousePosition()` and `MouseDelta()` are in scene pixels. `Main.cpp` makes the one call, with the four sizes `PresentPass::Execute` uses every frame. `Ui` did not change: it was always laying out in scene pixels, and it now gets a point in the same space.

**The mechanism was read off the screenshot, not guessed.** The owner's screenshot, as uploaded, is 2000×1333 — 3:2 — with bars top and bottom, so the scene is being scaled and letterboxed. NC-024's cross sits at scene (710, 418) to the pixel, which is the raw client coordinate the readout printed. That is only possible if the client coordinate went into the scene unconverted and the present step then scaled and shifted it a second time. The first mutation below reproduces exactly that number.

**Why the pixel's centre, and why rounding down.** At the centre, the way back lands on the texel a point sampler reads for that pixel — `PresentScaleTests`' `NearestSourceIndex` uses the same rule to measure the present step — so the pointer answers to what the glass shows under it. Rounding towards negative infinity keeps a pointer in a bar, or a drag past the window's edge under capture, outside the scene; C++'s truncation would fold it onto the scene's first row or column.

**Verified here, on Linux.** No Windows toolchain exists on this agent, so a clang 18.1.3 harness compiled the **real** `InputState.cpp` and the **real** `Fit` and `ScenePixelUnder`, cut verbatim out of `PresentPass.cpp`, against stand-ins for `<windows.h>`, D3D12 and CppUnitTest. It ran the real test methods: all 16 of `InputStateTests` and the four device-free methods of `GraphicsDeviceTests` (the two existing `Fit` tests and the two new ones). **20 of 20 pass** under `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshorten-64-to-32 -Werror`, the last flag because MSVC's C4244 slipped past a harness without it on NC-062.

**The tests fail against broken code.** Each of five deliberately broken variants, compiled in the same harness, fails at least one new test:

| Broken variant | Tests that fail | First failure |
|---|---|---|
| No mapping at all — **the defect itself** | 5 | `expected 604 got 710`: the raw coordinate the owner saw |
| Truncating towards zero instead of rounding down | 6 | the top bar's last row read as the scene's first |
| The placement's origin dropped | 4 | `expected 256 got 356`: off by the 117-pixel bar |
| The pixel's corner instead of its centre | 2 | the last pixel of a 1366×768 desk unreachable |
| The height used where the width belongs | 5 | `expected 604 got 1074` |

**The checkers.** `Build/CheckFormat.py` (239 files) and `Build/CheckProjectFiles.py` (9 projects) are clean; clang-format rewrote only lines this task added. clang-tidy 18 with the repository's `.clang-tidy` is clean over the changed code it can reach without the Windows SDK: `InputState.cpp`, the two `PresentPass` functions, `InputStateTests.cpp` and the new `GraphicsDeviceTests` methods, with `InputState.h` and `PresentPass.h` checked through the header filter. Two planted naming violations, one in a header, were both reported, so the silence is a result rather than a misconfiguration. It is a pre-filter only: `Main.cpp`'s one added call and the D3D half of the changed files are CI's, which pins 22.1.8 and runs it against the real Windows SDK.

**Not done by me, and not claimable:** no `msbuild`, no `vstest.console.exe`, no `RunClangTidy.py` in MSVC driver mode, no Release build and no executable run. CI is the build. **The desktop run is the owner's, and it is owed.** This task is not `Done` until someone has watched the cross sit under the pointer at the four corners of the picture on a display that is not 1920×1080.

**What CI then confirmed**, on the first push (run 76, `58d433d` merged with `main` at `2e508b5`): the MSVC `Debug|x64` build is clean under `/W4 /WX`; **428 of 428 tests pass** under the real CppUnitTest framework — the 420 already there and the 8 this task adds, 6 in `InputStateTests` and 2 in `GraphicsDeviceTests`; `RunClangTidy.py` reports **105 translation units clean** on clang-tidy 22.1.8; and the clang-format job is green. Every measured figure is unchanged, the NC-048 soak hash `6152001022014570065` among them, as it should be for a change that touches nothing in the simulation.

**`main` moved while this was open, onto the same two files.** `2e508b5` makes `PresentPass::Execute` bind the back buffer again before it presents, because the frame's own passes leave the scene target bound, and on the scaled path the present's draw was landing in the very target it reads. It is a different function from this task's and git merged the two without a conflict — but it is the other half of the same story, a defect that only a scaling display could show. The branch now carries it as a merge commit, so the desktop run this task still owes is made with both fixes and judges the mouse rather than the old present.

**Assumed:** a downscaled screenshot gives the owner's panel's shape, not its resolution. The tests use 2256×1504 as a representative 3:2 panel, and the (604, 256) in them is that panel's answer; the owner's panel may give a different number for the same pointer.

**Bent:** nothing. `InputState.h` now includes `PresentPass.h`, so input depends on the present step, which is the direction the knowledge flows: the present step decides where the scene is, and the mouse is told.

**Noticed and left alone.**

- **At a scale of one half or less, the scene's first pixel cannot be reached**: a client pixel's centre lands on texel 1 at exactly 2:1 down, the point-sampling rule working as written. No display a player has is that small — the smallest in the tests, 1280×720, is two-thirds — so the "whole desk is reachable" assertion is made over twelve real displays, and the sweep over `Fit`'s ten thousand sizes checks only that the placement lands inside the scene and the bars outside it.
- **The Windows pointer and NC-024's cross are both drawn**, and now sit on top of each other. Whether the desk hides the system pointer and draws its own is a presentation question for Phase 5, not this defect.
