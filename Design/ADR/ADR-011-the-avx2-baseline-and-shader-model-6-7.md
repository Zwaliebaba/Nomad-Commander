# ADR-011 — The AVX2 baseline and shader model 6.7

**Status:** Accepted
**Date:** 2026-09-16
**Task:** NC-021
**Cites:** AGENTS.md R14, R16, §3, §6; ADR-003 (the numeric model)

## Context

Visual Studio 2026 rewrote all nine `.vcxproj` files when the solution was opened on 2026-09-16. Two of its edits were not cosmetic: it set `EnableEnhancedInstructionSet` to `AdvancedVectorExtensions2` — `/arch:AVX2` — on every project, and it set `ShaderModel` 6.7 on the two HLSL items **under a `Debug|x64` condition only**, leaving the project default at 5.1.

Both were defects against the rules as they stood. R16 said "no `/arch`" in as many words, and a shader model that differs by configuration means Debug compiles with `dxc` and Release with `fxc` — two different compilers for the same source, which §3 exists to prevent. `CheckProjectFiles.py` reported ten findings and CI would have been red.

This came to a head in NC-021, which is the first task to add a shader since. Building a present pass against a toolchain that changes between configurations is not something to do and describe afterwards.

**The agent proposed reverting. The owner decided instead to adopt both as the project standard**, which is what this ADR records. A rule of `AGENTS.md` bent by a change is an ADR (§6), and this one is bent deliberately rather than quietly.

## Decision

1. **`/arch:AVX2` is the baseline.** `EnableEnhancedInstructionSet` is `AdvancedVectorExtensions2` in every `.vcxproj`, in the unconditioned `ItemDefinitionGroup` so that Debug and Release are identical, and stated rather than defaulted — which is the half of R16 that always mattered most.
2. **Shader model 6.7 is the default**, in `FXCompile`'s `ItemDefinitionGroup` and with no per-item condition, so both configurations compile every shader with `dxc` at `vs_6_7` / `ps_6_7`. `dxc` here is the one the Windows SDK installs, which R14 admits by name; the redistributable DirectX Shader Compiler remains excluded.
3. **Shader debug information is on in both configurations**, `EnableDebuggingInformation` true with `-Qembed_debug`. This is not a preference: `dxc` refuses `-Qembed_debug` without `/Zi`, `FXCompile` adds `/Zi` in Debug but not in Release, so the flag can only be unconditional if the debug information is too. The alternative — letting them differ — is the §3 violation this decision is fixing.
4. **R16 is amended**, and its new paragraph names the cost rather than waving at it.
5. **`CheckProjectFiles.py` requires `AdvancedVectorExtensions2`** instead of forbidding every `/arch`. The checker is the enforcement half of R16 and a rule the checker does not check is a rule that drifts.

## What this forecloses

**Every CPU without AVX2** — Intel before Haswell (2013), AMD before Excavator (2015). The failure is an illegal-instruction crash, not a message: nothing in the tree checks, and `/arch:AVX2` licenses the compiler to emit AVX2 in any translation unit, including start-up code that runs before any check could. Adding a friendly refusal is a separate decision and would have to run in a separately compiled, non-AVX2 translation unit to be worth anything.

**Every GPU or driver that cannot do shader model 6.7**, and this is the sharper of the two, because this is a graphics program and 6.7 is a 2022-era model. A device below it cannot create *any* pipeline state in this tree — every shader is 6.7 now, including the present pass, which is the one pass the game cannot do without. NC-021 turns that from a cryptic `CreateGraphicsPipelineState` failure into a named fault by querying `D3D12_FEATURE_SHADER_MODEL` at device creation and refusing with the model it found; that is a diagnosis, not a fallback, and there is no fallback.

**Bit-identical float results against a non-AVX2 build, and possibly against a differently optimised one.** `/arch:AVX2` lets MSVC contract `a*b+c` into an FMA even under `/fp:precise`, and whether it does can depend on the optimisation level — so Debug and Release may disagree about a float sum, which is precisely the symptom R16 was written to prevent.

**This is survivable only because the simulation holds no floats.** The rest of R16 requires integers and fixed point in `GameLogic` — the evidence weights are integer hundredths, ADR-003 fixes the numeric model — so the arithmetic the replay, the receipt and the Milestone 2 soak depend on is exact, and an FMA cannot reach it. Floats live in the renderer, where nothing is replayed and nobody compares two runs. **If a float ever enters `GameLogic`, this decision is what must be reopened**, and R16 now says so in its own text. That is the condition on which this ADR rests, and it is not a small one.

It does not foreclose going back: `NotSet` in nine files, 5.1 in one, and the checker constant restores the old rule exactly.

## Consequences

- Nine `.vcxproj` files carry `AdvancedVectorExtensions2`; `NeuronClient.vcxproj` carries `ShaderModel` 6.7, `EnableDebuggingInformation` and `-Qembed_debug` at the `FXCompile` item-definition level, and the two HLSL items carry only their `ShaderType` again.
- `AGENTS.md` R16 gains a paragraph; its first sentence changes from "no `/arch`" to `/arch:AVX2`.
- `Build/CheckProjectFiles.py` gains `INSTRUCTION_SET` and its Shape rule inverts on this point. Its docstring changes with it.
- Shader blobs grow, because debug information is embedded in Release too. For the handful of small shaders in this tree that is noise against the executable, and it makes a PIX capture readable in the configuration anyone would profile.
- **Unrelated, and fixed in the same pass because the checker could not otherwise go green:** the same Visual Studio rewrite had dropped the `Debug` and `Release` `BuildType` elements from `NomadCommander.slnx` and the final newline from all eighteen project files. Both are restored. Neither is part of this decision; they are the rest of the rewrite, put back.

## Measurements

- **Both configurations build clean** after the change: `msbuild NomadCommander.slnx /t:Rebuild` for `Debug|x64` and `Release|x64`, exit 0, zero warnings. Before it, Release failed with `dxc failed : Must enable debug info with /Zi for /Qembed_debug` (MSB6006, `dxc.exe` exit 1) while Debug succeeded — which is the configuration split this decision closes, caught by building the configuration CI does not build (§6).
- **The `dxc` invoked** is `C:\Program Files (x86)\Windows Kits\10\bin\10.0.28000.0\x86\dxc.exe`, from the Windows SDK, read off the `FxCompile` command line at `/v:normal`.
- **The highest shader model this machine's GPU reports** is recorded by NC-021's device query and named in that task's report; it was not measured here, and no CPU or GPU other than this developer's has been tried. **The AVX2 floor and the 6.7 floor above are read from Intel, AMD and Microsoft documentation, not measured** — this repository has one machine, and a hardware floor cannot be measured from inside it.
