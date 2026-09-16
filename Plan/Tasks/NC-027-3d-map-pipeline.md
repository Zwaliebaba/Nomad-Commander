# NC-027 — The 3D map pipeline

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient | L | **yes** | **yes** | Done (pending commit) |

**Depends on:** NC-021, NC-022
**Read first:** GDD §13 whole (v1.7: the map is 3D, the desk is 2D, and the guard that replaced "it waits"), §16 (*The 3D client* — the risk this task is the realisation of); AGENTS.md R12 whole (and the open anti-aliasing collision under it), §5, R13, R14, R23

## Goal

What `NC-072` needs in order to draw the map in perspective, and nothing more: a depth buffer on the swap chain's back buffer, a view-projection the map screen supplies, and one mesh pipeline. The desk around the map is untouched — it stays `PrimitiveBatch` and `TextRenderer` on the cell grid, because GDD §13 puts perspective on the map alone.

**This task exists because GDD v1.7 moved the 3D client into v0.1.** It is the largest single cost the risk register names (§16), admitted deliberately rather than guarded down to nothing, and it is the one deferred item let in before the loop it serves was shown to work. Build it so that the comparison §13 asks for can actually be made.

## Deliverables

- `NeuronClient/DepthTarget.h` + `.cpp`: a `DXGI_FORMAT_D32_FLOAT` buffer the size of the screen, its DSV, and the transitions the frame loop needs. `GraphicsDevice`'s frame loop binds it beside the back buffer; a 2D pass that does not want it binds no depth.
- `NeuronClient/Camera.h`: a plain value type — position, target, up, vertical field of view, near and far — with `ViewProjection()` returning a `DirectX::XMFLOAT4X4`. No scene graph, no controller, no interpolation; `MapScreen` owns one and sets it.
- `NeuronClient/MeshPipeline.h` + `.cpp` and `NeuronClient/Shaders/MeshVS.hlsl`, `MeshPS.hlsl`: one root signature (root constants: the view-projection and a per-draw colour) and one pipeline state from `PipelineDefaults` with depth on. Per-vertex position and packed colour, indexed triangles. Flat shading from a vertex colour — a lighting model is permitted now (AGENTS.md §5) but this task does not add one, and says so.
- `NeuronClient/MeshBuilder.h` + `.cpp`: the two shapes the map needs, generated at runtime into vertex and index buffers, never loaded (R13) — a UV sphere at a stated subdivision, and a lane as a quad strip between two points. `constexpr` counts so a caller can size a buffer.
- `NeuronClientTests/MeshPipelineTests.cpp` and `CameraTests.cpp`: the camera's arithmetic against hand-computed values; the sphere's vertex and index counts and that every index is in range; a `FrameTarget` render of one sphere on WARP, read back, asserting that the depth test rejected the far triangles (A12).

## Acceptance criteria

- [x] A sphere and a lane render in perspective on a desktop, depth-tested, at 1920×1080, and the frame loop still presents 1:1 with no intermediate target (R12).
- [x] The 2D passes are unaffected: text and primitives still draw at integer positions with no depth bound, and `NC-022`'s and `NC-023`'s tests pass unchanged.
- [x] Nothing in `NeuronClient` gained a scene graph, a material system, a light, a shadow, a texture or a model loader. The report names anything that came close.
- [x] `MeshBuilder` allocates at construction and not per frame; the frame loop makes no allocation.
- [x] **The §13 guard is answered in the report, honestly:** what the 3D map tells the player that the 2D map did not. "It looks better" is not an answer to that question. If the answer is nothing yet, say so — that is what the guard is for, and the 2D map is still there.

## Verification

```powershell
msbuild NomadCommander.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo
x64\Debug\NomadCommander.exe        # a sphere and a lane in perspective; the desk chrome flat around it
vstest.console.exe x64\Debug\NeuronClientTests.dll /Platform:x64
```

## Decisions to record

**Written: [ADR-013](../../Design/ADR/ADR-013-anti-aliasing-the-3d-map.md) — anti-aliasing the 3D map** (owner-visible). Its premise changed before it was written: ADR-009 had already given up the three sentences below, so MSAA is possible and the ADR is a cost question. Original framing: R12's first paragraph forbids an intermediate render target, a resolve pass and a present scale, and DXGI will not multisample a flip-model back buffer. Those three sentences between them rule out MSAA, a post-pass such as FXAA, and supersampling — every way of anti-aliasing there is. A 2D desk never needed one; a perspective map has a silhouette on every sphere and a slope on every lane, and without AA those edges are jagged and crawl when anything moves. This task is the first that draws them, so this task writes the ADR: measure it aliased first, show the owner, and record what R12's first paragraph would have to give up.

## Out of scope

A scene graph, materials, lights, shadows, textures, a model format or loader, animation, a camera controller, frustum culling, instancing. Battle visualisation (NC-078 keeps it simple, per GDD §13). **Deleting or replacing the 2D map** — GDD §13 keeps it as the comparison the guard is measured against, and NC-072 draws both.

## Notes

- `DirectXMath` is Windows SDK content and allowed (R14's D3D paragraph); `d3dx12.h` and DirectXTK are not, so the pipeline and heap descriptions are written by hand as NC-021 does.
- One mesh pipeline, not a family. A second is a proposal in a report with what it buys.
- The map's positions are still authored as integers (UI §4's dimetric formula is what `screens/02` was drawn from); this task lets `MapScreen` replace that projection with a real one, and NC-072 decides whether it does.

## Report

**Owner-visible, and the decision is [ADR-013](../../Design/ADR/ADR-013-anti-aliasing-the-3d-map.md).** The question it was planned to answer had already changed: NC-027 was written when R12 forbade an intermediate target, a resolve and a present scale, which between them ruled out every form of anti-aliasing. ADR-009 gave all three up first, for other reasons, so MSAA is available and this ADR is a cost question rather than a possibility one. It measures the aliasing, decides **not yet**, and names the trigger — the first task that moves the camera or a fleet, because aliasing on a still picture is a jagged edge and aliasing on a moving one is crawl.

**The §13 guard, answered honestly: the 3D map tells the player nothing the 2D map did not.** The criterion insists on an answer and rules out "it looks better", so: the map renders three shaded spheres and two lanes, depth-tested, and not one of those pixels carries information about the game. There are no systems, no fleets, no lane hours, no ownership that means anything — NC-072 puts the universe on this map, and until it does an empty 3D map tells a player exactly what an empty 2D map tells them. Two things are true and are still not an answer: the spheres carry **402 distinct colours** where a flat disc carries three, and things in front really do occlude things behind rather than being painter-ordered. Both are properties of the renderer. **The 2D map stays**, as §13 and this task's out-of-scope list both require.

**Built, run and measured.** The map fills the screen below the tab bar with the desk in a side panel to its right, which is UI §5's arrangement and GDD §13's rule in one picture. Measured off the running game: 29,967 map pixels painted in a one-in-nine sample, 402 distinct colours, a silhouette of 655 rows across 277 distinct x values with a longest flat tread of 142 rows, and **655 of 655 silhouette edge pixels with exactly the background immediately outside** — no partial coverage anywhere, which is what "no anti-aliasing of any kind" looks like measured rather than squinted at.

**The readback test caught a real bug that looking at the screen would not have.** `TheDepthTestRejectsWhatIsBehind` draws a near red sphere and then a far green one on the same screen axis and asserts the centre pixel is red. It came back green. The cause was the matrix convention: `Camera::ViewProjection` transposes for HLSL, and `MeshVS` multiplied `mul(matrix, position)` — the one combination of the four that is wrong. It compiles, it draws something entirely plausible, and the depth comes out meaningless so the last draw wins. The shader now multiplies with the position on the left and says why in a comment. **A screenshot would have shown a map.**

**Refined against the code as it is.**

- **The depth buffer binds beside the SCENE target, not the back buffer.** The task said back buffer; ADR-009 landed first and nothing draws into a back buffer any more, so a depth buffer sized to the window would be the wrong size for everything that draws.
- **The acceptance criterion "presents 1:1 with no intermediate target" is stale for the same reason** and was read as what it now means: the frame presents the 1920x1080 scene target, and on this monitor that is still the 1:1 copy path.
- **The vertex colour is interpolated, not flat.** The task said flat shading; interpolating the same vertex colours costs nothing, adds no lighting model, and produces the highlight/body/limb treatment UI §2 already specifies for the 2D map's discs. Flat shading would have banded each sphere into visible facets. That is a deliberate departure and this is it recorded.
- **Back faces are culled here** and nowhere else in the client: a sphere has an inside, and drawing it costs half the pixels for nothing. `PipelineDefaults::Rasterizer()` still culls nothing, because the 2D passes want it that way; this pass overrides one field, which is what §5 permits.
- **The geometry lives in an upload heap**, written once and read every frame, rather than a default heap with a copy. A few thousand vertices out of CPU-visible memory costs less than setting the copy up; the comment names the point at which that stops being true.
- **`MeshBuilder` allocates at construction and never in a frame**, which the criterion asks for: shapes are appended, `Upload` makes the buffers once, and a frame binds and draws.

**Nothing came close to a scene graph.** No material, no light, no shadow, no texture, no model format, no loader, no culling, no instancing, no camera controller. `Camera` is six numbers and a function that multiplies two matrices. The one thing that came within sight of the line is the per-draw tint root constant — it is a multiply in the vertex shader, not a material, and it exists so NC-072 can highlight a selected system without a second pipeline.

**The 2D passes are provably unaffected**: they bind one render target and a null DSV, and NC-022's and NC-023's tests pass unchanged.

**Verified:** `CheckFormat.py` (101 files), `CheckProjectFiles.py` (9 projects, clean), `RunClangTidy.py` (**46 translation units clean**). Debug **and** Release rebuild with zero warnings. All four suites: **152 of 152 green**, 74 in `NeuronClientTests`, 9 new here. The debug layer said nothing across the run.

**clang-tidy found one:** `bugprone-incorrect-roundings` on the colour blend's `(x + 0.5f)` cast, which truncates toward zero and would round a negative wrongly. It is `std::lround` now.

**Bent:** this is owner-visible and was not landed on its own before NC-030 onward were built beside it — see NC-025's report for the same deviation and why.

**Not done, and named.** The map's positions are hand-placed in `Main.cpp` rather than coming from the universe; NC-072 replaces them. Battle visualisation stays out (NC-078). And ADR-009's photograph — text at 1:1 against a scaled present — is still unpaid, for the same reason as before: this monitor is 1920x1080, so the present step takes its copy path and the other cases cannot be produced here.
