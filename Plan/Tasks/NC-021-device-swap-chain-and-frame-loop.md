# NC-021 — The device, the swap chain and the frame loop

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient | L | **yes** | no | Open |

**Depends on:** NC-020, NC-006
**Read first:** AGENTS.md R12 whole (format, no intermediate target, no D3D11, COM is RAII), R13, R14 (and the D3D12 paragraph under it), §4 (include order; `<windows.h>` before D3D12/DXGI), §5 (*The renderer starts with no sampler, no blending and no multisampling*); `Plan/Roadmap.md` A12

## Goal

The Direct3D 12 device, its direct queue, and a two-buffer flip-model swap chain of 1280×720 `R8G8B8A8_UNORM` on the window, with a frame loop that clears the back buffer, presents with vsync, and fences correctly. Everything the renderer will ever need from D3D12 that is not a pipeline lives here; nothing here knows what a rectangle is.

## Deliverables

- `NeuronClient/GraphicsDevice.h` + `.cpp`: `class GraphicsDevice`, `struct Desc { bool useWarp; bool enableDebugLayer; }`, `Create`, `Device()`, `Queue()`, `Factory()`; adapter choice: the first hardware adapter that creates a `D3D_FEATURE_LEVEL_11_0` device, or WARP when asked or when none does; the debug layer and `ID3D12InfoQueue` set to break on error and corruption in `_DEBUG`.
- `NeuronClient/SwapChainTarget.h` + `.cpp`: `class SwapChainTarget`, `enum class TargetFault : std::uint8_t { DeviceRemoved, BadFormat, OutOfVideoMemory }` (AGENTS.md's worked example, made real), `Create(GraphicsDevice&, HWND, const Desc&)`, `BeginFrame()` returning the open command list with the back buffer transitioned to render target and cleared to a colour, `EndFrame()` (transition to present, close, execute, `Present(1, 0)`, signal), a per-buffer allocator and fence value, `WaitForGpu()`, and `Fault()` after a failed present.
- `NeuronClient/FrameTarget.h` + `.cpp` (test-only use in the game, but engine code): an offscreen 1280×720 `R8G8B8A8_UNORM` texture with the same `BeginFrame`/`EndFrame` shape plus `ReadBack(std::vector<std::uint32_t>&)`, so NeuronClientTests can render on WARP and inspect pixels (A12).
- `NomadCommander/Main.cpp` grows a loop: pump, begin frame, end frame, until closed; a device-removed fault ends the process with a message in the debug output and a non-zero exit.
- `#pragma comment(lib, …)` for `d3d12.lib`, `dxgi.lib`, `dxguid.lib` in `GraphicsDevice.cpp`.
- `NeuronClientTests/GraphicsDeviceTests.cpp` (WARP device creates; a `FrameTarget` clears to a colour and the readback shows that colour in every pixel).

## Acceptance criteria

- [ ] The window shows a solid clear colour with no tearing and no scaling; the report names the GPU and confirms `DXGI_FORMAT_R8G8B8A8_UNORM` (not `_SRGB`) and `DXGI_SWAP_EFFECT_FLIP_DISCARD` from a PIX or debug-layer capture, or from the code plus the debug layer being silent.
- [ ] The debug layer reports nothing across one thousand frames in Debug.
- [ ] Every COM object is a `Microsoft::WRL::ComPtr`; `grep -n "AddRef\|Release()" NeuronClient` finds nothing (R12).
- [ ] `Alt+Enter` does nothing (`DXGI_MWA_NO_ALT_ENTER`).
- [ ] A frame never overwrites a back buffer the GPU is still reading: the fence per buffer is waited before reuse; the test double-checks by rendering two hundred frames on WARP without a debug-layer complaint.
- [ ] `GraphicsDeviceTests` pass on the CI runner (WARP, no window).

## Verification

```powershell
msbuild NomadCommander.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo /warnaserror
x64\Debug\NomadCommander.exe        # a cleared 1280×720 window; watch the debug output for the info queue
vstest.console.exe x64\Debug\NeuronClientTests.dll /Platform:x64
```

## Decisions to record

**ADR — the test-only offscreen target on WARP.** `FrameTarget` exists so CI can exercise the pipelines with no GPU and no window; the game never creates one (R12's "no intermediate render target" is about the game's path). What it forecloses: nothing; it is the reason a later "the game could just render to a texture too" is an ADR and not a habit.

## Out of scope

Any pipeline, root signature or shader use (NC-022); MSAA; a depth buffer (the game is 2D and painter-ordered); HDR; device-removed recovery (report and exit is v0.1).

## Notes

- Include order in every `.cpp` here: `pch.h`, then `NeuronCore.h` (which brings `<windows.h>`), then `<d3d12.h>`, `<dxgi1_6.h>`, `<wrl/client.h>`, then project headers, then the standard library (AGENTS.md §4).
- No `d3dx12.h`: write the `D3D12_RESOURCE_BARRIER` and heap descriptions by hand, in a small `D3D12Helpers.h` if repetition demands it (R14).
- `CreateSwapChainForHwnd` with `BufferCount` 2, `Scaling` `DXGI_SCALING_NONE`, `AlphaMode` ignored, `Flags` 0; `SetMaximumFrameLatency` is optional and, if used, an ADR is not needed.
- `FrameTarget` readback: a `D3D12_HEAP_TYPE_READBACK` buffer, `CopyTextureRegion` with the 256-byte row pitch, map after a fence wait.

## Report

_Filled in on hand-back._
