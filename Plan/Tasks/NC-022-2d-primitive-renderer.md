# NC-022 — The 2D primitive renderer

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient | L | **yes** | no | Open |

**Depends on:** NC-021
**Read first:** GDD §13 (2D map, "the game's complexity is informational, not visual"); AGENTS.md §5 (*The client is 2D, and the design protects that*; *no sampler, no blending, no multisampling*), R12, R13 (shaders at build time)

## Goal

Everything the map and the UI will draw that is not text: filled rectangles, lines, filled convex polygons and circles, in pixel coordinates with the origin at the top left, in painter's order, opaque, through one root signature and two pipeline states built from shared defaults that have blending, multisampling and anti-aliased lines off.

## Deliverables

- `NeuronClient/Shaders/PrimitiveVS.hlsl` and `PrimitivePS.hlsl` (replacing NC-006's stubs): the vertex shader turns `float2` pixel positions into clip space using the screen size from root constants and passes a `uint` packed colour through; the pixel shader unpacks and returns it.
- `NeuronClient/PipelineDefaults.h` + `.cpp`: `PipelineDefaults::Rasterizer()` (cull none, `AntialiasedLineEnable` false, `MultisampleEnable` false), `Blend()` (no blend, write all), `DepthStencil()` (disabled), `SampleDesc()` (count 1), and a comment quoting AGENTS.md §5: a pass that wants otherwise writes an ADR.
- `NeuronClient/PrimitivePipeline.h` + `.cpp` (grown from NC-006): the root signature (root constants only), two `ID3D12PipelineState`s (triangle list, line list), `Create(GraphicsDevice&)`.
- `NeuronClient/PrimitiveBatch.h` + `.cpp`: `struct PrimitiveVertex { float positionX; float positionY; std::uint32_t colorRgba; }` (R8; `float` is fine on the client, R16 binds GameLogic), a per-frame upload ring (`FRAMES_IN_FLIGHT` slices of a persistently mapped upload buffer), `Begin(commandList)`, `FillRect`, `Rect` (outline, 1 px), `Line`, `FillPolygon(std::span<const Point>)`, `FillCircle(centre, radius, segments)`, `End()` which records the draws in submission order.
- `NeuronClientTests/PrimitiveBatchTests.cpp`: on WARP into a `FrameTarget`, fill a 10×10 rectangle at (20, 30) with `0xFF0000FF` and read back exactly those pixels changed; draw a horizontal line and read back its row.

## Acceptance criteria

- [ ] The executable shows rectangles, lines, a polygon and a circle at the pixel positions the code states; the report includes what was drawn and where (a screenshot in the PR is welcome).
- [ ] The readback tests pass on WARP with pixel-exact assertions; a colour authored `0xAA` is read back `0xAA` (R12: `_UNORM`, not `_SRGB`).
- [ ] The two pipeline states are built from `PipelineDefaults` and nothing overrides a default (the reviewer reads the PSO description).
- [ ] The upload ring never writes a slice the GPU may still read (it is indexed by the frame's back-buffer slot and fenced by NC-021).
- [ ] A batch of 50,000 vertices in one frame draws without a debug-layer message; the report states the measured frame time on the machine used.

## Verification

```powershell
x64\Debug\NomadCommander.exe        # the test pattern the task's Main.cpp draws
vstest.console.exe x64\Debug\NeuronClientTests.dll /Platform:x64
```

## Decisions to record

None. Blending, a sampler or MSAA would be one (AGENTS.md §5); this task adds none.

## Out of scope

Text (NC-023), textures, transforms other than pixel-to-clip, a scene graph, a camera (AGENTS.md §5 names the camera as a step towards 3D that nobody has).

## Notes

- Root constants: `uint2 screenSize` is enough; the shader computes `ndc = (pos / screenSize) * float2(2, -2) + float2(-1, 1)`.
- Circles are polygons; segments scale with radius; the map's nodes are small.
- Lines are `D3D_PRIMITIVE_TOPOLOGY_LINELIST`, one pixel wide, aliased by design.
- Keep `Main.cpp`'s test pattern; NC-025 replaces it with the UI's.

## Report

_Filled in on hand-back._
