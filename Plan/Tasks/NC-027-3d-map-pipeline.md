# NC-027 — The 3D map pipeline

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 1 | NeuronClient | L | **yes** | **yes** | Open |

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

- [ ] A sphere and a lane render in perspective on a desktop, depth-tested, at 1920×1080, and the frame loop still presents 1:1 with no intermediate target (R12).
- [ ] The 2D passes are unaffected: text and primitives still draw at integer positions with no depth bound, and `NC-022`'s and `NC-023`'s tests pass unchanged.
- [ ] Nothing in `NeuronClient` gained a scene graph, a material system, a light, a shadow, a texture or a model loader. The report names anything that came close.
- [ ] `MeshBuilder` allocates at construction and not per frame; the frame loop makes no allocation.
- [ ] **The §13 guard is answered in the report, honestly:** what the 3D map tells the player that the 2D map did not. "It looks better" is not an answer to that question. If the answer is nothing yet, say so — that is what the guard is for, and the 2D map is still there.

## Verification

```powershell
msbuild NomadCommander.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo
x64\Debug\NomadCommander.exe        # a sphere and a lane in perspective; the desk chrome flat around it
vstest.console.exe x64\Debug\NeuronClientTests.dll /Platform:x64
```

## Decisions to record

**ADR — anti-aliasing the 3D map** (owner-visible). R12's first paragraph forbids an intermediate render target, a resolve pass and a present scale, and DXGI will not multisample a flip-model back buffer. Those three sentences between them rule out MSAA, a post-pass such as FXAA, and supersampling — every way of anti-aliasing there is. A 2D desk never needed one; a perspective map has a silhouette on every sphere and a slope on every lane, and without AA those edges are jagged and crawl when anything moves. This task is the first that draws them, so this task writes the ADR: measure it aliased first, show the owner, and record what R12's first paragraph would have to give up.

## Out of scope

A scene graph, materials, lights, shadows, textures, a model format or loader, animation, a camera controller, frustum culling, instancing. Battle visualisation (NC-078 keeps it simple, per GDD §13). **Deleting or replacing the 2D map** — GDD §13 keeps it as the comparison the guard is measured against, and NC-072 draws both.

## Notes

- `DirectXMath` is Windows SDK content and allowed (R14's D3D paragraph); `d3dx12.h` and DirectXTK are not, so the pipeline and heap descriptions are written by hand as NC-021 does.
- One mesh pipeline, not a family. A second is a proposal in a report with what it buys.
- The map's positions are still authored as integers (UI §4's dimetric formula is what `screens/02` was drawn from); this task lets `MapScreen` replace that projection with a real one, and NC-072 decides whether it does.

## Report

_Filled in on hand-back._
