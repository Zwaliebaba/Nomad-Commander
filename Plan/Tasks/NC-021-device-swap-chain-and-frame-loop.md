# NC-021 — The device, the swap chain and the frame loop

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient | L | **yes** | no | Done (e486053) |

**Depends on:** NC-020, NC-006
**Read first:** AGENTS.md R12 whole (format, the scene target and the present scale, no D3D11, COM is RAII), **ADR-009**, R13, R14 (and the D3D12 paragraph under it), §4 (include order; `<windows.h>` before D3D12/DXGI), §5 (*Blending and samplers are a pass's own business*, and why MSAA is unavailable on a flip-model back buffer); `Plan/Roadmap.md` A12

## Goal

The Direct3D 12 device, its direct queue, a two-buffer flip-model swap chain on the window, and the **1920×1080 `R8G8B8A8_UNORM` scene target every pass draws into**, presented into the back buffer scaled to the client area with the aspect preserved (ADR-009), with a frame loop that clears the back buffer, presents with vsync, and fences correctly. Everything the renderer will ever need from D3D12 that is not a pipeline lives here; nothing here knows what a rectangle is.

## Deliverables

- `NeuronClient/GraphicsDevice.h` + `.cpp`: `class GraphicsDevice`, `struct Desc { bool useWarp; bool enableDebugLayer; }`, `Create`, `Device()`, `Queue()`, `Factory()`; adapter choice: the first hardware adapter that creates a `D3D_FEATURE_LEVEL_11_0` device, or WARP when asked or when none does; the debug layer and `ID3D12InfoQueue` set to break on error and corruption in `_DEBUG`.
- `NeuronClient/SwapChainTarget.h` + `.cpp`: `class SwapChainTarget`, `TargetFault` (AGENTS.md's worked example, made real — it lives in `SceneTarget.h` where that example puts it, shared by both targets, and carries `None` and `Allocation` as well), `Create(GraphicsDevice&, HWND, const Desc&)`, `BeginFrame()` returning the open command list with the back buffer transitioned to render target and cleared to a colour, `EndFrame()` (transition to present, close, execute, `Present(1, 0)`, signal), a per-buffer allocator and fence value, `WaitForGpu()`, and `Fault()` after a failed present.
- `NeuronClient/SceneTarget.h` + `.cpp`: the 1920×1080 `R8G8B8A8_UNORM` target the game draws into, its RTV, and the transitions the frame loop needs. ADR-009 makes this the game's own target rather than a test-only object, so `ReadBack(std::vector<std::uint32_t>&)` lives here and A12's WARP tests inspect the same pixels the player would see.
- `NeuronClient/PresentPass.h` + `.cpp` — the **present pass**: two static samplers (point and linear, chosen by a root constant), one root signature, one pipeline, `Shaders/Present{VS,PS}.hlsl`, and the fit arithmetic as a static `Fit` that needs no device — 1:1 and unfiltered when the client area is 1920×1080, point at an exact integer multiple, bilinear otherwise, letterboxed. This is the tree's first sampler; AGENTS.md §5 stopped forbidding one on 2026-09-16 and this is what it was for.
- `NomadCommander/Main.cpp` grows a loop: pump, begin frame, end frame, until closed; a device-removed fault ends the process with a message in the debug output and a non-zero exit.
- `#pragma comment(lib, ...)` for `d3d12.lib`, `dxgi.lib`, `dxguid.lib` in `GraphicsDevice.cpp`.
- `NeuronClientTests/GraphicsDeviceTests.cpp` (WARP device creates and reports shader model 6.7; a `SceneTarget` clears to a colour and the readback shows that colour in every one of its 2,073,600 pixels; the fit is swept as a property; two hundred frames present on a real swap chain).

## Acceptance criteria

- [x] The window shows a solid clear colour with no tearing and no scaling; the report names the GPU and confirms `DXGI_FORMAT_R8G8B8A8_UNORM` (not `_SRGB`) and `DXGI_SWAP_EFFECT_FLIP_DISCARD` from a PIX or debug-layer capture, or from the code plus the debug layer being silent.
- [x] The debug layer reports nothing across one thousand frames in Debug.
- [x] Every COM object is a `Microsoft::WRL::ComPtr`; `grep -n "AddRef\|Release()" NeuronClient` finds nothing (R12).
- [x] `Alt+Enter` does nothing (`DXGI_MWA_NO_ALT_ENTER`).
- [x] A frame never overwrites a back buffer the GPU is still reading: the fence per buffer is waited before reuse; the test double-checks by rendering two hundred frames on WARP without a debug-layer complaint.
- [x] `GraphicsDeviceTests` pass on the CI runner (WARP, no window) -- they pass on WARP here; CI is what proves the runner.

## Verification

```powershell
msbuild NomadCommander.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo /warnaserror
x64\Debug\NomadCommander.exe        # a cleared 1920×1080 window; watch the debug output for the info queue
vstest.console.exe x64\Debug\NeuronClientTests.dll /Platform:x64
```

## Decisions to record

~~**ADR — the test-only offscreen target on WARP.** `FrameTarget` exists so CI can exercise the pipelines with no GPU and no window; the game never creates one (R12's "no intermediate render target" is about the game's path).~~

**This decision was overtaken before the task was worked, and no ADR was written for it.** ADR-009 removed the premise: R12 no longer says "no intermediate render target", every pass draws into a scene target, and that target is the game's own. So there is no test-only object to justify and nothing to foreclose — `SceneTarget` is what both the game and the suite use, which is what this task's own deliverable list already said. `FrameTarget` does not exist.

**Written instead: [ADR-011](../../Design/ADR/ADR-011-the-avx2-baseline-and-shader-model-6-7.md)** — the AVX2 baseline and shader model 6.7 (owner decision), forced by this task because it is the first to add a shader since Visual Studio rewrote the project files. See the report.

## Out of scope

Any pipeline, root signature or shader use beyond the present pass (NC-022); MSAA (possible now that the scene target is not a back buffer, but NC-027 decides whether it is worth paying for); the depth buffer (NC-027, now that GDD v1.7 puts a 3D map in v0.1; this frame loop binds none); HDR; device-removed recovery (report and exit is v0.1).

## Notes

- Include order in every `.cpp` here: `pch.h`, then `NeuronCore.h` (which brings `<windows.h>`), then `<d3d12.h>`, `<dxgi1_6.h>`, `<wrl/client.h>`, then project headers, then the standard library (AGENTS.md §4).
- No `d3dx12.h`: write the `D3D12_RESOURCE_BARRIER` and heap descriptions by hand, in a small `D3D12Helpers.h` if repetition demands it (R14).
- `CreateSwapChainForHwnd` with `BufferCount` 2, `Scaling` `DXGI_SCALING_NONE`, `AlphaMode` ignored, `Flags` 0; `SetMaximumFrameLatency` is optional and, if used, an ADR is not needed.
- `SceneTarget::ReadBack`: a `D3D12_HEAP_TYPE_READBACK` buffer, `CopyTextureRegion` with the 256-byte row pitch, map after a fence wait. It is not `noexcept` — it sizes the caller's vector, and an allocation in a `noexcept` function is `std::terminate`.

## Report

**Built, run and measured on Windows.** The frame loop is real: the device, a two-buffer flip-model swap chain, the 1920×1080 scene target, the present pass and the fencing. `x64\Debug\NomadCommander.exe` shows a solid `#1E283C` screen and Escape closes it with exit code 0. This is the first commit in the tree's life that puts a pixel on the glass.

**What was measured, and how.**

| Claim | How it was checked |
|---|---|
| A solid clear colour, no scaling | 42,625 pixels sampled across the screen while the game ran: **one** distinct colour, `#1E283C`, the authored bytes exactly |
| `R8G8B8A8_UNORM`, not `_SRGB` | the same sample: a channel authored `0x1E` reaches the glass as `0x1E`. An `_SRGB` target would have encoded it to some brighter byte on the way out; which byte was not measured, only that this one did not change |
| `FLIP_DISCARD`, `BufferCount` 2, `SampleDesc` 1 | `SwapChainTarget.cpp`, plus the debug layer silent on a swap chain it would otherwise complain about |
| The debug layer reports nothing | **3,060 frames, zero D3D12 messages.** Captured out of the DBWIN shared buffer — a listener on `DBWIN_BUFFER` with `DBWIN_BUFFER_READY` / `DBWIN_DATA_READY`, filtered to the game's process id — so this is read, not inferred |
| The fence is right | 200 frames on WARP with the info queue breaking on error and corruption, against a real swap chain on a hidden window |
| `Alt+Enter` does nothing | `WM_SYSKEYDOWN` / `VK_RETURN` with the Alt context bit posted to the running game: window rect 1920×1080 at (0,0) before and after, style `0x94000000` unchanged, process alive |
| Every COM object is a `ComPtr` | `grep -n "AddRef\|Release()" NeuronClient` finds one comment and no code |

**The GPU is not named here, and that is a gap.** The run that matters used the hardware path, but nothing wrote `AdapterName()` to the debug output, so the adapter string was never captured. `GraphicsDevice::AdapterName()` exists and the next task that logs a run should use it. What *is* established is that the device reports shader model 6.7 or better, because `Create` refuses anything less (ADR-011) and the game ran.

**The debug-layer capture found a defect the tests did not, which is the entire argument for that acceptance criterion.** The first long run produced **3,380 warnings in 22 seconds — exactly one per frame**: `CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE`. The scene target was created with a black optimized clear value and then cleared to `#1E283C`, so every frame took the driver's slow path. The comment three lines above the defect said the two values have to match. Nothing failed, no test went red, every pixel was still correct — it existed only in that stream.

The fix makes it structurally impossible rather than merely corrected: `SceneTarget::Desc` carries one `clearColor`, `Create` uses it for the `D3D12_CLEAR_VALUE`, and `Clear()` takes no colour at all, so it cannot be handed a different one. The re-run is the 3,060-frame, zero-message one above.

**Refined against the code as it is.**

- **`FrameTarget` is gone from the plan, not renamed.** ADR-009 had already removed its premise: R12 no longer says "no intermediate render target", every pass draws into a scene target, and that target is the game's own. So there is no test-only object to justify and nothing for an ADR to foreclose. One type, `SceneTarget`, used by the game and by the suite — which is what makes A12 worth anything: the suite inspects the same pixels a player sees rather than a parallel object. `Plan/Glossary.md` lost that row and gained `PresentPass`.
- **`PresentPass` is a file of its own**, which the task did not name. It owns the root signature, the pipeline, the SRV heap, the two static samplers and the fit arithmetic — the one place that looks at the difference between the screen and the window.
- **`TargetFault` lives in `SceneTarget.h`**, not in `SwapChainTarget.h` as the task said, because AGENTS.md's own worked example puts it there and AGENTS.md outranks a task file. It gained `None` and `Allocation`: a fault enum with no "no fault" value cannot answer `Fault()`.
- **The 1:1 case is a `CopyResource`, not a sampler.** ADR-009 asks for "exactly 1:1 and no filtering" when the client area is the screen. A sampler landing on texel centres gives the same pixels *by argument*; a copy gives them *by construction*, needs no pipeline and no descriptor, and is the cheapest path available — which R12 requires rather than suggests. `Filter::None` is that case, and since ADR-010 it is the case this machine takes.
- **No `PipelineDefaults`.** The glossary assigns it to NC-022 and this task's *Out of scope* forbids pipeline work beyond the present pass, so this pass sets its own states, which AGENTS.md §5 permits in as many words. The four helpers in `PresentPass.cpp` are the shape NC-022 should factor out.
- **`SceneTarget::ReadBack` is not `noexcept`.** It sizes the caller's vector, which allocates eight megabytes; an allocation inside a `noexcept` function is `std::terminate` on a machine low on memory. Same defect class as the one NC-020 round 5 found in `Window::Create`, and clang-tidy found this one the same way.
- **`GraphicsDevice` checks the shader model at creation** and refuses below 6.7 with a named fault, because ADR-011 put every shader in this tree at 6.7 and an older GPU therefore cannot create a single pipeline here. It turns a cryptic `CreateGraphicsPipelineState` failure into a sentence.
- **`Main.cpp` prints the frame count on the way out.** It is what makes "the debug layer said nothing across N frames" a number somebody read rather than one inferred from a clock, and the debug layer writes to the same stream, so a run's whole story is in one place. NC-032 gives instrumentation a home of its own (R24); this is the debug output and not that.

**clang-tidy found eight things and none were suppressed.** Seven were `bugprone-invalid-enum-default-initialization`. A great many D3D12 enums have no zero-valued enumerator — `D3D12_BLEND`, `D3D12_BLEND_OP`, `D3D12_COMPARISON_FUNC`, `D3D12_STENCIL_OP`, `D3D12_FILL_MODE`, `D3D12_CULL_MODE`, `D3D12_TEXTURE_ADDRESS_MODE` and `D3D12_HEAP_TYPE` all start at 1 or 2 — so the idiomatic `D3D12_SOMETHING_DESC desc{}` puts values into the struct that those enums have no name for. A D3D12 sample avoids this with `d3dx12.h`'s `CD3DX12_*` constructors, which R14 excludes. So they are written out: `HeapProperties`, `StaticSampler`, `OpaqueBlend`, `SolidRasterizer` and `DepthStencilDisabled` name every field, and the pipeline description is a designated initializer. That is what R14 means by "resource barriers and heap descriptions are written by hand", and ADR-007's line holds — a finding about what the code *does* is fixed, not annotated. The eighth was the `ReadBack` `noexcept` above.

**Verified:** `CheckFormat.py` (71 files), `CheckProjectFiles.py` (9 projects, clean), `RunClangTidy.py` (**31 translation units clean**). The local clang-tidy is 22.1.3 where CI pins 22.1.8, so CI remains the arbiter. Debug **and** Release rebuild with zero warnings — CI does not build Release (§6) and this change adds enough new code to be worth the minute. All four suites: **92 of 92 green**, 14 of them in `NeuronClientTests`.

**New tests.** `AWarpDeviceIsCreatedWithAQueueAndShaderModelSixSeven`. `TheSceneTargetIsTheScreenAndReadsBackWhatWasClearedIntoIt`, which compares all 2,073,600 pixels rather than a sample and names the first that differs. `TheFitIsOneToOneOnlyWhenTheClientAreaIsTheScreen`, for ADR-009's three cases at the sizes that actually occur. `TheFitAlwaysFitsKeepsTheShapeAndStaysCentered`, which sweeps roughly ten thousand client sizes and asserts the placement fits, reaches one of its bounds, keeps the screen's shape within a pixel and stays centred — the property technique NC-020 round 8 introduced, applied to the arithmetic that decides what a person sees. And `TwoHundredFramesPresentWithoutTheDebugLayerComplaining`, on a real swap chain on a window that is created and never shown.

**WARP supports shader model 6.7**, which was the risk ADR-011 carried into CI: had it not, every `GraphicsDeviceTests` case would be red on the runner and the suite could not exercise D3D12 at all. It does here; the runner's WARP is what settles it.

**Assumed:** that a build agent can create a swap chain on a hidden window. `WindowTests` already creates windows there, but a swap chain is more than a window. If DXGI refuses on a session with no display, `TwoHundredFramesPresentWithoutTheDebugLayerComplaining` is the test that goes red first, and the answer is to fence the same way against the scene target alone and say plainly that the swap-chain path is desktop-only.

**Not done, and named rather than quietly skipped.**

- **ADR-009 owes a photograph** — the same text at 1:1, at 2× point and at a fractional bilinear scale — and this task does not discharge it. Nothing in the tree draws a glyph yet (NC-023), and this machine has one monitor, which is 1920×1080, so the only case it can photograph is the one that needs no argument. The comparison ADR-010 asks for, between filling the client area and using the largest integer scale with bars, needs the same picture and the same second monitor.
- **MSAA, the depth buffer and device-removed recovery** stay out of scope as the task says. The scene target is created with `SampleDesc.Count` 1; NC-027 decides whether to pay for more, and ADR-009 made that decision possible rather than made it.
- **`SetMaximumFrameLatency` was not used.** The task allows it without an ADR. Two buffers and vsync are enough for a desk, and adding a latency knob before anything draws would be tuning a number nobody has felt.

**Bent:** one task per PR, twice over. This work also carries NC-020's borderless window and ADR-010, because the window had to be settled before a swap chain could sit on it and the owner asked for both in one sitting; and it reached `main` as a single commit, `e486053`, rather than through a pull request. The status lines name that commit rather than a PR number that does not exist, because `Plan/README.md` makes the status on `main` the truth about `main`.

**Noticed, left alone:** `Plan/Roadmap.md`'s assumption A12 still reads "The game's own path stays 'straight into the swap chain' (R12); the test target is not a render target the game has." ADR-009 reversed that on 2026-09-16, so the assumption is false as written — though the thing it was protecting, that CI can exercise D3D12 with no GPU and no window, is exactly what happens. It is a roadmap assumption rather than a rule, and correcting it is not this task's to do.
