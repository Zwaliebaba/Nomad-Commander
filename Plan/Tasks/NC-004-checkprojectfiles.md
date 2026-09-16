# NC-004 — `Build/CheckProjectFiles.py`

| Phase | Project(s) | Size | Desktop run | Owner-visible | Status |
|---|---|---|---|---|---|
| 0 | Build | L | no | no | Open |

**Depends on:** NC-002
**Read first:** AGENTS.md §1 (R2, R7, R11 and the *Enforcement* table), §2 (the project graph, the flat-directory rule, the shader directories), §3 (*Debug and Release are aligned by rule*, the include-path rule), §6 (*Keep the project files honest*); `Plan/Roadmap.md` *Conventions*; `.clang-tidy` (`HeaderFilterRegex`, and the four rules it says this script carries)

## Goal

The static gate that stands in for the Release build nobody runs and for the rules clang-tidy cannot see: the build shape of every `.vcxproj`, Debug/Release alignment, project and filter registration against the disk, flat directories, R2 affixes, R7 file names, R11 spellings, unique header names, the include edges of the project graph, and the `Wire*.h` seam. It runs first in CI because it is two seconds and it catches what would otherwise surface as a confusing build failure.

## Deliverables

- `Build/CheckProjectFiles.py`, standard library only, exit 0 on a clean tree and 1 with one line per finding: `<path>:<line>: <rule>: <message>`.
- The checks, each named in the output by its rule:
  1. **Shape.** Every `.vcxproj`: only `x64` configurations; `PlatformToolset` v145; `LanguageStandard` stdcpplatest; `ConformanceMode` true; `WarningLevel` Level4; `TreatWarningAsError` true; `FloatingPointModel` Precise; `EnableEnhancedInstructionSet` absent or NotSet; `CharacterSet` Unicode; `PrecompiledHeader` Use; no member of the Windows macro family in any `PreprocessorDefinitions`; no project's own directory in its `AdditionalIncludeDirectories`; cross-project include directories spelled `$(SolutionDir)<Project>`; `OutDir` anchored on `$(SolutionDir)`.
  2. **Alignment.** For each project, every `ClCompile`, `Link`, `Lib`, `FXCompile` and `Manifest` property is identical between Debug and Release except: `UseDebugLibraries`, `Optimization`, `FunctionLevelLinking`, `IntrinsicFunctions`, `WholeProgramOptimization`, `LinkIncremental`, `EnableCOMDATFolding`, `OptimizeReferences`, `RuntimeLibrary` (only the pair MultiThreadedDebugDLL/MultiThreadedDLL), and `PreprocessorDefinitions` differing exactly by `_DEBUG` vs `NDEBUG`. The allowlist is a named constant at the top of the script with a comment citing AGENTS.md §3.
  3. **Registration.** Every `.cpp`, `.h` and `.hlsl` on disk under a project directory is in its `.vcxproj` and its `.filters`, with byte-identical spelling, and every listed file exists. `CompiledShaders/` is skipped and must not be committed (`git ls-files` shows nothing under it).
  4. **Flat.** No subdirectory under a project directory except `Shaders/` and `CompiledShaders/`.
  5. **R7.** Source files are `PascalCase.h`/`.cpp`, or one of `pch.h`, `pch.cpp`, `framework.h`, `targetver.h`, `Resource.h`; no `.hpp`, `.cc`, `.inl`. Shaders are `<Name>VS.hlsl`/`<Name>PS.hlsl`, and each has an `FXCompile` item whose header output is `CompiledShaders\<Name>VS.h`/`PS.h` and whose variable is `g_<Name>VS`/`PS`.
  6. **Unique names.** No two headers in the tree share a base name, except the per-project files R7 exempts (`pch.h`, `framework.h`, `targetver.h`, `Resource.h`), which never cross a project boundary (Roadmap *Conventions*).
  7. **R2.** No `class`/`struct`/`enum`/`concept`/`using` declaration whose name starts with `I`, `C`, `S`, `E` followed by an uppercase letter and is followed by another uppercase-led word (`IFoo`, `CFoo`), or ends in `Base`, `Abstract`, `Impl` or `_t`. SDK identifiers are not declared by this tree, so the rule needs no exemption list; if one becomes necessary, it is a named constant with a reason.
  8. **R11.** No identifier (comments and string literals stripped) contains, case-insensitively, `colour`, `initialise`, `serialise`, `normalise`, `quantise`, `synchronise`, `behaviour`, `neighbour`, `centre`, `grey` or `cancelled`.
  9. **Edges.** Resolving each `#include "…"` the way `cl.exe` does (the including file's directory, then the project's include directories in order): NeuronCore includes only itself; NeuronClient, NeuronServer and GameLogic include only themselves and NeuronCore; NeuronClient never includes NeuronServer or the reverse; no client-side file (all of NeuronClient; all of NomadCommander except `App.cpp`) includes a GameLogic header other than `Wire*.h`; `Wire*.h` include only NeuronCore and each other; a test project includes only the libraries it references. Windows and standard headers are ignored.
  10. **Solution.** `NomadCommander.slnx` lists exactly the nine projects and only the `x64` platform.
- A `--list-rules` option printing the ten rules with one line each, so the script documents itself.

## Acceptance criteria

- [ ] Exit 0 on the tree as NC-002 left it.
- [ ] For each of the ten rules, a deliberate violation made in a scratch copy is reported with the right rule name and path; the report says which ten violations were tried.
- [ ] Runs in under five seconds on the Phase-0 tree, on the CI runner too.
- [ ] `build.yml`'s *Check the build shape* step passes as written.

## Verification

```powershell
python Build\CheckProjectFiles.py
python Build\CheckProjectFiles.py --list-rules
```

## Decisions to record

**ADR — include edges and the `Wire*.h` seam.** The convention from `Plan/Roadmap.md`: the wire schema lives in `GameLogic/Wire*.h`, client-side files see GameLogic only through it, `App.cpp` is the one file that sees both halves. Context: R18 and AGENTS.md §2 ("a client-side file that reaches for GameLogic is a client that can see through the fog"). What it forecloses: a client that includes a game header for convenience.

## Out of scope

Anything clang-tidy checks (§1's *Enforcement* table). Formatting (NC-003). Running the build.

## Notes

- Parse `.vcxproj` with `xml.etree`; the MSBuild namespace is `http://schemas.microsoft.com/developer/msbuild/2003`. Condition attributes carry the configuration; compare the *effective* property set per configuration, not the file order.
- Rule 8 must strip `//` and `/* */` comments and string literals before matching, or it will fire on the GDD's spelling quoted in a comment, which AGENTS.md R11 explicitly permits.
- Rule 9's resolution is why rule 6 exists: with unique base names, "which project owns `Random.h`" has one answer.
- Keep the rules independent so that one failing does not hide another; collect all findings, then exit.

## Report

_Filled in on hand-back._
