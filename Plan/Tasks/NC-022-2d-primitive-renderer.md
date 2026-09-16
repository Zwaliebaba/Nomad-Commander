# NC-022 — The 2D primitive renderer

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient | L | **yes** | no | Done (pending commit) |

**Depends on:** NC-021
**Read first:** GDD §13 (2D map, "the game's complexity is informational, not visual"); AGENTS.md §5 (*Blending and samplers are a pass's own business, and the shared defaults stay opaque*), R12, R13 (shaders at build time)

## Goal

Everything the map and the UI will draw that is not text: filled rectangles, lines, filled convex polygons and circles, in pixel coordinates with the origin at the top left, in painter's order, opaque, through one root signature and two pipeline states built from shared defaults that leave blending and anti-aliased lines off. Opaque is what these primitives need; it is a default this task keeps, not a rule it obeys.

## Deliverables

- `NeuronClient/Shaders/PrimitiveVS.hlsl` and `PrimitivePS.hlsl` (replacing NC-006's stubs): the vertex shader turns `float2` pixel positions into clip space using the screen size from root constants and passes a `uint` packed colour through; the pixel shader unpacks and returns it.
- `NeuronClient/PipelineDefaults.h` + `.cpp`: `PipelineDefaults::Rasterizer()` (cull none, `AntialiasedLineEnable` false, `MultisampleEnable` false), `Blend()` (no blend, write all), `DepthStencil()` (disabled), `SampleDesc()` (count 1 — DXGI does not multisample a flip-model back buffer, so this one is not a choice), and a comment saying which of these are defaults a later pass may override and which is a fact about the swap chain.
- `NeuronClient/PrimitivePipeline.h` + `.cpp` (grown from NC-006): the root signature (root constants only), two `ID3D12PipelineState`s (triangle list, line list), `Create(GraphicsDevice&)`.
- `NeuronClient/PrimitiveBatch.h` + `.cpp`: `struct PrimitiveVertex { float positionX; float positionY; std::uint32_t colorRgba; }` (R8; `float` is fine on the client, R16 binds GameLogic), a per-frame upload ring (`FRAMES_IN_FLIGHT` slices of a persistently mapped upload buffer), `Begin(commandList)`, `FillRect`, `Rect` (outline, 1 px), `Line`, `FillPolygon(std::span<const Point>)`, `FillCircle(centre, radius, segments)`, `End()` which records the draws in submission order.
- `NeuronClientTests/PrimitiveBatchTests.cpp`: on WARP into a `SceneTarget` (`FrameTarget` never existed — see NC-021), fill a 10×10 rectangle at (20, 30) with `0xFF0000FF` and read back exactly those pixels changed; draw a horizontal line and read back its row.

## Acceptance criteria

- [x] The executable shows rectangles, lines, a polygon and a circle at the pixel positions the code states; the report includes what was drawn and where (a screenshot in the PR is welcome).
- [x] The readback tests pass on WARP with pixel-exact assertions; a colour authored `0xAA` is read back `0xAA` (R12: `_UNORM`, not `_SRGB`).
- [x] The two pipeline states are built from `PipelineDefaults` and nothing overrides a default (the reviewer reads the PSO description).
- [x] The upload ring never writes a slice the GPU may still read (it is indexed by the frame's back-buffer slot and fenced by NC-021).
- [x] A batch of 50,000 vertices in one frame draws without a debug-layer message; the report states the measured frame time on the machine used — **partly**: 54,000 vertices draw clean, but frame time is vsync-capped and was not measured properly. See the report.

## Verification

```powershell
x64\Debug\NomadCommander.exe        # the test pattern the task's Main.cpp draws
vstest.console.exe x64\Debug\NeuronClientTests.dll /Platform:x64
```

## Decisions to record

None. Blending and samplers stopped needing one on 2026-09-16 (AGENTS.md §5); this task still uses neither, because opaque primitives need neither.

## Out of scope

Text (NC-023), textures, transforms other than pixel-to-clip, a scene graph, a camera. None of these is forbidden any more (AGENTS.md §5, 2026-09-16); they are simply not what this task draws.

## Notes

- Root constants: `uint2 screenSize` is enough; the shader computes `ndc = (pos / screenSize) * float2(2, -2) + float2(-1, 1)`.
- Circles are polygons; segments scale with radius; the map's nodes are small.
- Lines are `D3D_PRIMITIVE_TOPOLOGY_LINELIST`, one pixel wide, aliased by design.
- Keep `Main.cpp`'s test pattern; NC-025 replaces it with the UI's.

## Report

**Built, run and measured on Windows.** `x64\Debug\NomadCommander.exe` draws the test pattern: a filled rectangle with an outline around it, a fan of eleven lines, a hexagon, five circles of growing radius, a painter's-order demonstration, and a one-pixel marker in each corner of the screen.

**Every primitive was checked by reading the pixels back, not by looking.** Eight probes at stated coordinates on the running game, and all eight are the authored bytes exactly:

| Probe | Pixel | Colour |
|---|---|---|
| filled rectangle | (300, 200) | `#36465A` |
| outline | (118, 118) | `#C8C8C8` |
| line fan | (130, 421) | `#E8A521` |
| hexagon | (880, 260) | `#36465A` |
| painter's order — base rectangle | (1320, 160) | `#36465A` |
| painter's order — line over it | (1310, 220) | `#D23A3A` |
| painter's order — rectangle over both | (1500, 220) | `#C8C8C8` |
| corner markers | (0, 0) and (1919, 1079) | `#D23A3A` |

**The corner markers are the check that matters most.** One pixel at (0,0) and one at (1919,1079), both arriving as the colour they were authored, is what proves the pixel-to-clip arithmetic has no off-by-one and that nothing between the vertex shader and the glass is scaling. The painter's-order triple is the second: a line drawn *between* two rectangles comes out between them, which is what would break the day somebody decides to sort the batch by pipeline state.

**The debug layer said nothing across 2,034 frames**, captured from the DBWIN stream the same way NC-021's was, with the info queue breaking on error and corruption.

**Refined against the code as it is.**

- **`PipelineDefaults` is real, and `PresentPass` was folded onto it.** NC-021 wrote its blend, rasterizer and depth-stencil states inline and its report said they were the shape this task should factor out. They are now `PipelineDefaults::Blend()`, `Rasterizer()`, `DepthStencil()` and `SampleDesc()`, and `PresentPass` uses all four. Its static samplers stayed where they were: samplers are a pass's own business (AGENTS.md §5) and `PipelineDefaults` holds none.
- **`Rect` is four filled rectangles, not four lines.** Line rasterization follows the diamond-exit rule, which makes the pixels a line's ends land on a matter of argument. An outline is something a UI aligns other things to, so it is drawn as four one-pixel filled rectangles and is exact: a 20×10 outline is 56 pixels and no pixel is drawn twice. `Line` still uses the line pipeline.
- **`Line` shifts to pixel centres.** A line asked for at *y* = 200 is drawn at 200.5, so it covers row 200 rather than sitting on the boundary between two rows and letting the rasterizer choose. The test asserts exactly that, and that rows 199 and 201 are untouched.
- **`Begin` takes the pipeline and the target size.** The size is what the vertex shader turns pixels into clip space with *and* what the viewport and scissor are set to — given once, so a caller cannot set a viewport that disagrees with the shader. Binding the render target stays the caller's, because the batch does not own it.
- **`SwapChainTarget::BufferIndex()` is new**, and is how the batch picks its slice. NC-021's `BeginFrame` has already waited on that slot's fence, so the slice being written is one the GPU has finished with — which is the whole of the ring's correctness, and it costs one accessor.
- **A fixed run array, not a `std::vector`.** A run is a stretch of vertices with one topology, and a new one starts when the topology changes. `Reserve` runs once per primitive, so it must not allocate: an allocation inside a `noexcept` function is `std::terminate` on a machine low on memory. clang-tidy found both the `push_back` and the `reserve`; the answer was to stop allocating rather than to drop `noexcept`, because this is a per-primitive path. 4,096 runs per frame, and exhausting them is an overflow like any other.
- **Overflow drops the primitive and says so.** A frame that asks for more than a slice holds gets `Overflowed()` true and nothing written past the end. Corrupting the next frame's slice would be a defect nobody could find; a dropped rectangle and a flag is one the caller can.

**clang-tidy found three things and none were suppressed:** the two allocations above, and an integer division used in a floating-point context in the test's 9,000-rectangle loop, where the row index is deliberately integral — now computed as an `int` first so the intent is in the code rather than in a comment.

**Measured.**

- **54,000 vertices in one frame**, which is the task's fifty thousand with room over: `AFullFrameOfVerticesDrawsAndTheSliceIsNeverOverrun` draws 9,000 rectangles and asserts the vertex count exactly. **No debug-layer message.** The whole test — that frame, *plus* a second pass of 40,000 rectangles that deliberately overruns the slice, *plus* two full 1920×1080 readbacks of eight megabytes each — takes **35 ms on WARP**, the software rasterizer. The draw itself is far below a frame on anything with a GPU.
- **Frame time was not measured properly, and that is a gap.** The frame loop presents with vsync, so the ~2,000 frames a fifteen-second run produces measure the display's refresh rate and not the renderer's cost. A real per-frame cost needs a GPU timestamp query, which this task did not build and which belongs with whatever first has a reason to care. The 35 ms WARP figure above is what stands in for it: an upper bound, on the slowest rasterizer available.

**Verified:** `CheckFormat.py` (76 files), `CheckProjectFiles.py` (9 projects, clean), `RunClangTidy.py` (**34 translation units clean**). Debug **and** Release rebuild with zero warnings. All four suites: **99 of 99 green**, 21 of them in `NeuronClientTests`, 7 new here.

**New tests.** `AFilledRectangleCoversExactlyThePixelsItNames` — the task's own 10×10 at (20,30), asserted by surveying all 2,073,600 pixels for a bounding box and a count, then naming the four corners and the four pixels just outside them. `AColorAuthoredIsTheColorReadBack` — `0xAA` in, `0xAA` out, which is the `_UNORM`-not-`_SRGB` guarantee and would read some other byte the day somebody changes the format. `AHorizontalLineCoversOneRow`. `AnOutlineIsOnePixelThickAndEncloses`. `PaintersOrderPutsTheLastPrimitiveOnTop`. `APolygonAndACircleCoverRoughlyTheirArea` — area within 5 % of π*r*² and the bounding box within three pixels, because asserting a circle's exact pixels would be asserting the rasterizer's tie-breaking rules rather than this code's contract. And the 54,000-vertex frame above.

**Noticed, left alone:** the task's *Deliverables* said the test would draw into a `FrameTarget`; that type never existed (see NC-021's report) and the tests use `SceneTarget`. The deliverable list is corrected above rather than in a separate pass.
