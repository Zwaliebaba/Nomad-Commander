# NC-006 — The shader pipeline: `FXCompile` into `CompiledShaders/`

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 0 | NeuronClient | S | no | no | Done (PR #1) |

**Depends on:** NC-001
**Read first:** AGENTS.md §2 (the two sanctioned subdirectories), R13 (*Shaders are compiled at build time*), §4 (`.editorconfig` for `.hlsl`)

## Goal

Prove the build-time shader path end to end: a vertex and a pixel shader in `NeuronClient/Shaders/`, compiled by the `.vcxproj`'s `FXCompile` step into `NeuronClient/CompiledShaders/<Name>VS.h` and `<Name>PS.h` as `g_<Name>VS`/`g_<Name>PS`, included by exactly one `.cpp`, with nothing on disk beside the executable and nothing committed under `CompiledShaders/`. NC-022 fills the shaders with the real 2D pipeline; this task fixes the mechanics.

## Deliverables

- `NeuronClient/Shaders/PrimitiveVS.hlsl` and `PrimitivePS.hlsl`: the smallest shaders that compile (a `float4 main(float2 _position : POSITION) : SV_Position` and a `float4 main() : SV_Target`).
- `FXCompile` items in `NeuronClient.vcxproj` for both, identical in Debug and Release: `ShaderType` Vertex/Pixel, `ShaderModel` 5.1, `EntryPointName` main, `HeaderFileOutput` `$(ProjectDir)CompiledShaders\%(Filename).h`, `VariableName` `g_%(Filename)`, `ObjectFileOutput` empty (no `.cso`).
- `NeuronClient/PrimitivePipeline.h` + `.cpp`: for now, two functions returning `std::span<const std::byte>` over `g_PrimitiveVS` and `g_PrimitivePS`; NC-022 grows the file into the pipeline builder. The `.cpp` is the only file that includes `CompiledShaders/…`.
- `.filters` entries under a `Shaders` filter.

## Acceptance criteria

- [ ] A clean build produces both headers; a second build does not rewrite them when the `.hlsl` is unchanged (incremental).
- [ ] `git status` after a build shows nothing under `CompiledShaders/` (`.gitignore` already lists it).
- [ ] A `NeuronClientTests` test (this deletes `SuiteSmoke` there) asserts both spans are non-empty and begin with the bytes `DXBC`.
- [ ] No `.cso` and no `d3dcompiler_47.dll` in `x64\Debug\` (R13).
- [ ] `Build/CheckProjectFiles.py` rule 5 passes on the two shaders.

## Verification

```powershell
msbuild NomadCommander.slnx /p:Configuration=Debug /p:Platform=x64 /m /v:minimal /nologo /warnaserror
dir NeuronClient\CompiledShaders
vstest.console.exe x64\Debug\NeuronClientTests.dll /Platform:x64
git status --porcelain
```

## Decisions to record

None. Shader model 5.1 through `fxc` is the SDK's default path for `FXCompile`; a move to 6.x (`dxc`, also in the SDK) would be an ADR when a feature needs it.

## Out of scope

The real primitive shaders, a root signature, a pipeline state (NC-022).

## Notes

- The HLSL entry-point parameters may follow the C++ naming table (`_position`); nothing enforces it, and consistency is the reason.
- `FXCompile`'s default `HeaderFileOutput` is empty; setting it is what turns on header output.
- The `CompiledShaders/` directory does not exist in a fresh clone, and `fxc.exe` does not create the directory of its `/Fh` output; a `CreateCompiledShadersDirectory` target in `NeuronClient.vcxproj` runs `MakeDir` before `FXCompile`.

## Report

**Verified here (Linux, no `fxc`):** `CheckProjectFiles.py` passes with the two shaders registered, and its R7 shader sub-rule fires in a scratch copy on a wrong `VariableName`, on a shader not named `<Name>VS`/`<Name>PS`, and (`Edges`) on a second `.cpp` including a compiled header; `CheckFormat.py` passes on the new `.cpp`/`.h`; `ClientSmoke.cpp` and NeuronClientTests' `SuiteSmoke.cpp` are deleted now that a real translation unit and a real test exist. **Verified by CI, not here:** that `FXCompile` produces both headers, that the build is incremental, that the two `PrimitivePipelineTests` pass, and that nothing lands beside the executable.

**Assumed:** `ObjectFileOutput` left empty emits no `/Fo` and produces no `.cso`; if the `FXCompile` target insists on an object path, the fallback is `$(IntDir)%(Filename).cso` (intermediate output, not beside the executable), recorded here if it is needed.

**Refined:** the `MakeDir` target above, since `fxc.exe` will not create `CompiledShaders\`; `TreatWarningAsError` on `FXCompile` so a shader warning is as fatal as a C++ one.

**Bent:** R7 for `PrimitivePipeline.h`, which holds two functions and no type until NC-022 adds the class the task already names.
