#!/usr/bin/env python3
"""Build/CheckProjectFiles.py: the build shape, the project registries and the rules clang-tidy cannot see.

AGENTS.md §1 (R2, R7, R11), §2 (the project graph, flat directories, the shader directories), §3 (Debug and Release
aligned by rule; the include-path rule) and §6 (keep the project files honest). It runs first in CI because it is
seconds and it catches what would otherwise surface as a confusing build failure or, worse, a build that passes
against a stale object. Nothing beyond the Python standard library.

Exit 0 on a clean tree; 1 with one line per finding, `<path>[:<line>]: <rule>: <message>`; 2 on a usage error.
`--list-rules` prints the ten rules.

The ten rules, in the order they run:

  Shape         Every .vcxproj: x64 only; v145; stdcpplatest; /permissive-; /W4 /WX; /fp:precise; no /arch; Unicode;
                a precompiled header; none of the Windows macro family in PreprocessorDefinitions; no project's own
                directory on its include path; cross-project include directories spelled $(SolutionDir)<Project>;
                OutDir anchored on $(SolutionDir).
  Alignment     Debug and Release differ only in ALLOWED_TO_DIFFER (the machine-readable form of AGENTS.md §3) and in
                _DEBUG versus NDEBUG.
  Registration  Every .cpp, .h and .hlsl on disk under a project directory is in its .vcxproj and its .filters with
                byte-identical spelling, and every listed file exists; nothing under CompiledShaders/ is committed.
  Flat          No subdirectory under a project directory except Shaders/ and CompiledShaders/.
  R7            PascalCase.h/.cpp only (plus pch.h, pch.cpp, framework.h, targetver.h, Resource.h); shaders are
                <Name>VS.hlsl / <Name>PS.hlsl with an FXCompile item writing CompiledShaders\\<Name>VS.h as g_<Name>VS.
  UniqueNames   No two headers in the tree share a base name, except R7's per-project files.
  R2            No type named with an I/C/S/E prefix or a Base/Abstract/Impl/_t suffix.
  R11           No identifier spelled colour, initialise, serialise, normalise, quantise, synchronise, behaviour,
                neighbour, centre, grey or cancelled (comments and strings are not identifiers).
  Edges         Includes and ProjectReferences follow the project graph of AGENTS.md §2 and ADR-001: the client sees
                GameLogic only through Wire*.h, from App.cpp excepted; NeuronClient and NeuronServer never meet; a
                CompiledShaders header is included by one .cpp.
  Solution      NomadCommander.slnx lists exactly the nine projects and only the x64 platform.
"""
import argparse
import pathlib
import re
import subprocess
import sys
import xml.etree.ElementTree as ElementTree

ROOT = pathlib.Path(__file__).resolve().parent.parent
MSBUILD_NAMESPACE = "http://schemas.microsoft.com/developer/msbuild/2003"

# name -> directory relative to the root. The order is the reference order of AGENTS.md §2.
PROJECTS = {
  "NeuronCore": "NeuronCore",
  "NeuronClient": "NeuronClient",
  "NeuronServer": "NeuronServer",
  "GameLogic": "GameLogic",
  "NomadCommander": "NomadCommander",
  "NeuronCoreTests": "Tests/NeuronCoreTests",
  "NeuronClientTests": "Tests/NeuronClientTests",
  "NeuronServerTests": "Tests/NeuronServerTests",
  "GameLogicTests": "Tests/GameLogicTests",
}
TEST_PROJECTS = {name for name in PROJECTS if name.endswith("Tests")}

# The project graph (AGENTS.md §2): what each project may include from and reference. Self is implied.
ALLOWED_DEPENDENCIES = {
  "NeuronCore": set(),
  "NeuronClient": {"NeuronCore"},
  "NeuronServer": {"NeuronCore"},
  "GameLogic": {"NeuronCore"},
  "NomadCommander": {"NeuronCore", "NeuronClient", "NeuronServer", "GameLogic"},
  "NeuronCoreTests": {"NeuronCore"},
  "NeuronClientTests": {"NeuronClient", "NeuronCore"},
  "NeuronServerTests": {"NeuronServer", "NeuronCore"},
  "GameLogicTests": {"GameLogic", "NeuronCore"},
}
# ADR-001: the composition root is the one client-side file that sees GameLogic beyond Wire*.h.
COMPOSITION_ROOT = "App.cpp"
WIRE_HEADER = re.compile(r"^Wire[A-Z][A-Za-z0-9]*\.h$")

CONFIGURATIONS = ("Debug|x64", "Release|x64")

# AGENTS.md §3: the four things Debug and Release differ in, as the MSBuild properties that spell them.
ALLOWED_TO_DIFFER = {
  ("Property", "UseDebugLibraries"),
  ("Property", "WholeProgramOptimization"),
  ("Property", "LinkIncremental"),
  ("ClCompile", "Optimization"),
  ("ClCompile", "FunctionLevelLinking"),
  ("ClCompile", "IntrinsicFunctions"),
  ("ClCompile", "RuntimeLibrary"),
  ("ClCompile", "WholeProgramOptimization"),
  ("Link", "EnableCOMDATFolding"),
  ("Link", "OptimizeReferences"),
  ("Link", "LinkTimeCodeGeneration"),
  ("Lib", "LinkTimeCodeGeneration"),
}
RUNTIME_LIBRARY_PAIR = {"MultiThreadedDebugDLL", "MultiThreadedDLL"}
CONFIGURATION_DEFINES = {"_DEBUG", "NDEBUG"}

# AGENTS.md §4: NeuronCore.h owns these and nothing else defines any of them.
WINDOWS_MACRO_FAMILY = {"NOMINMAX", "WIN32_LEAN_AND_MEAN", "NODRAWTEXT", "NOGDI", "NOBITMAP", "NOMCX", "NOSERVICE",
                        "NOHELP"}
UNIT_TEST_INCLUDE = "$(VCInstallDir)Auxiliary\\VS\\UnitTest\\include"

SANCTIONED_SUBDIRECTORIES = {"Shaders", "CompiledShaders"}
BUILD_OUTPUT_DIRECTORIES = {"CompiledShaders", "x64", ".vs"}
SOURCE_EXTENSIONS = {".cpp", ".h"}
BANNED_EXTENSIONS = {".hpp", ".cc", ".cxx", ".inl", ".hh", ".hxx", ".c"}
R7_EXCEPTIONS = {"pch.h", "pch.cpp", "framework.h", "targetver.h", "Resource.h"}
PASCAL_CASE_FILE = re.compile(r"^[A-Z][A-Za-z0-9]*\.(cpp|h)$")
SHADER_FILE = re.compile(r"^([A-Z][A-Za-z0-9]*)(VS|PS)\.hlsl$")

# R2: a prefix letter followed by an uppercase-led word (IFoo, CFoo, SFoo, EFoo), or a banned suffix.
BANNED_TYPE_PREFIX = re.compile(r"^[ICSE][A-Z][a-z]")
BANNED_TYPE_SUFFIX = re.compile(r"(Base|Abstract|Impl|_t)$")
TYPE_DECLARATION = re.compile(
  r"\b(?:class|struct|union|concept|enum(?:\s+(?:class|struct))?)\s+([A-Za-z_][A-Za-z0-9_]*)")
ALIAS_DECLARATION = re.compile(r"\busing\s+([A-Za-z_][A-Za-z0-9_]*)\s*=")
IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
# R11: the losing half of each spelling family, matched inside identifiers, case-insensitively.
BANNED_SPELLINGS = ("colour", "initialise", "serialise", "normalise", "quantise", "synchronise", "behaviour",
                    "neighbour", "centre", "grey", "cancelled")
QUOTED_INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.MULTILINE)


class Findings:
  """Collects `<path>[:<line>]: <rule>: <message>` lines; every rule runs, then the script exits once."""

  def __init__(self):
    self.lines = []

  def add(self, rule, path, message, line=None):
    location = f"{path}:{line}" if line else str(path)
    self.lines.append(f"{location}: {rule}: {message}")


def relative(path):
  return pathlib.Path(path).resolve().relative_to(ROOT).as_posix()


def tag(element):
  return element.tag.replace(f"{{{MSBUILD_NAMESPACE}}}", "")


def condition_matches(condition, configuration, findings, path):
  """Evaluates the configuration conditions this tree uses; anything else is a finding, not a guess."""
  if condition is None:
    return True
  text = condition.replace(" ", "")
  name, platform = configuration.split("|")
  match = re.fullmatch(r"'\$\(Configuration\)\|\$\(Platform\)'=='([^']*)'", text)
  if match:
    return match.group(1) == configuration
  match = re.fullmatch(r"'\$\(Configuration\)'=='([^']*)'", text)
  if match:
    return match.group(1) == name
  match = re.fullmatch(r"'\$\(Platform\)'=='([^']*)'", text)
  if match:
    return match.group(1) == platform
  findings.add("Shape", path, f"unsupported condition {condition!r}; only configuration conditions are evaluated")
  return False


class Project:
  """One .vcxproj, read into the effective settings of each configuration."""

  def __init__(self, name, findings):
    self.name = name
    self.directory = ROOT / PROJECTS[name]
    self.path = self.directory / f"{name}.vcxproj"
    self.filters_path = self.directory / f"{name}.vcxproj.filters"
    self.relative = relative(self.path)
    self.configurations = []
    self.properties = {configuration: {} for configuration in CONFIGURATIONS}
    self.definitions = {configuration: {} for configuration in CONFIGURATIONS}
    self.items = {}  # item type -> list of (include, metadata dict)
    self.references = []
    self.filter_items = set()
    self.findings = findings
    self.loaded = self._load()

  def _load(self):
    if not self.path.is_file():
      self.findings.add("Registration", self.relative, "project file is missing")
      return False
    try:
      root = ElementTree.parse(self.path).getroot()
    except ElementTree.ParseError as error:
      self.findings.add("Shape", self.relative, f"not well-formed XML: {error}")
      return False
    for element in root:
      kind = tag(element)
      condition = element.get("Condition")
      if kind == "ItemGroup" and element.get("Label") == "ProjectConfigurations":
        self.configurations = [child.get("Include") for child in element if tag(child) == "ProjectConfiguration"]
      elif kind == "PropertyGroup":
        for configuration in CONFIGURATIONS:
          if condition_matches(condition, configuration, self.findings, self.relative):
            for child in element:
              self.properties[configuration][tag(child)] = (child.text or "").strip()
      elif kind == "ItemDefinitionGroup":
        for configuration in CONFIGURATIONS:
          if condition_matches(condition, configuration, self.findings, self.relative):
            for item in element:
              metadata = self.definitions[configuration].setdefault(tag(item), {})
              for child in item:
                # MSBuild's %(Name) inside a value is the value so far; expand it, or a later group would hide an
                # earlier one and a define leaked through the shared group would never be seen.
                name = tag(child)
                value = (child.text or "").strip()
                metadata[name] = value.replace(f"%({name})", metadata.get(name, ""))
      elif kind == "ItemGroup":
        if condition is not None:
          self.findings.add("Shape", self.relative,
                            "an ItemGroup with a condition: files belong to both configurations")
        for item in element:
          item_kind = tag(item)
          metadata = {tag(child): (child.text or "").strip() for child in item}
          if item_kind == "ProjectReference":
            self.references.append(item.get("Include", ""))
          else:
            self.items.setdefault(item_kind, []).append((item.get("Include", ""), metadata))
    if self.filters_path.is_file():
      try:
        filters_root = ElementTree.parse(self.filters_path).getroot()
        for group in filters_root:
          if tag(group) == "ItemGroup":
            for item in group:
              if tag(item) in ("ClCompile", "ClInclude", "FXCompile"):
                self.filter_items.add(item.get("Include", ""))
      except ElementTree.ParseError as error:
        self.findings.add("Registration", relative(self.filters_path), f"not well-formed XML: {error}")
    else:
      self.findings.add("Registration", relative(self.filters_path), "filters file is missing")
    return True

  def property(self, name, configuration="Debug|x64"):
    return self.properties[configuration].get(name)

  def metadata(self, item, name, configuration="Debug|x64"):
    return self.definitions[configuration].get(item, {}).get(name)

  def listed_files(self):
    """Include attributes of ClCompile, ClInclude and FXCompile items, as written (backslashes and all)."""
    return {include for kind in ("ClCompile", "ClInclude", "FXCompile") for include, _ in self.items.get(kind, [])}

  def include_directories(self):
    value = self.metadata("ClCompile", "AdditionalIncludeDirectories") or ""
    return [entry for entry in value.split(";") if entry and not entry.startswith("%(")]

  def on_disk_files(self):
    """Source files under the project directory, as paths relative to it with forward slashes."""
    files = []
    for path in sorted(self.directory.rglob("*")):
      parts = path.relative_to(self.directory).parts
      if any(part in BUILD_OUTPUT_DIRECTORIES for part in parts):
        continue
      if path.is_file() and path.suffix in SOURCE_EXTENSIONS | BANNED_EXTENSIONS | {".hlsl"}:
        files.append(path)
    return files


def split_list(value):
  return [entry for entry in (value or "").split(";") if entry and not entry.startswith("%(")]


def check_shape(project, findings):
  rule = "Shape"
  path = project.relative
  if sorted(project.configurations) != sorted(CONFIGURATIONS):
    findings.add(rule, path,
                 f"project configurations are {project.configurations}; only Debug|x64 and Release|x64 exist")
  expected_properties = {"PlatformToolset": "v145", "CharacterSet": "Unicode"}
  for name, expected in expected_properties.items():
    actual = project.property(name)
    if actual != expected:
      findings.add(rule, path, f"{name} is {actual!r}, must be {expected!r}")
  expected_compile = {
    "LanguageStandard": "stdcpplatest",
    "ConformanceMode": "true",
    "WarningLevel": "Level4",
    "TreatWarningAsError": "true",
    "FloatingPointModel": "Precise",
    "PrecompiledHeader": "Use",
    "PrecompiledHeaderFile": "pch.h",
  }
  for name, expected in expected_compile.items():
    actual = project.metadata("ClCompile", name)
    if actual != expected:
      findings.add(rule, path, f"ClCompile.{name} is {actual!r}, must be {expected!r}")
  instruction_set = project.metadata("ClCompile", "EnableEnhancedInstructionSet")
  if instruction_set not in (None, "", "NotSet"):
    findings.add(rule, path, f"ClCompile.EnableEnhancedInstructionSet is {instruction_set!r}; no /arch (R16)")
  defines = set()
  for configuration in CONFIGURATIONS:
    defines |= set(split_list(project.metadata("ClCompile", "PreprocessorDefinitions", configuration)))
  leaked = sorted(defines & WINDOWS_MACRO_FAMILY)
  if leaked:
    findings.add(rule, path,
                 f"PreprocessorDefinitions defines {leaked}; NeuronCore.h owns the Windows macro family (§4)")
  own = {"$(ProjectDir)", ".", "./", ".\\", f"$(SolutionDir){project.name}", f"$(SolutionDir){project.name}\\"}
  for entry in project.include_directories():
    if entry in own:
      findings.add(rule, path, f"include directory {entry!r} is the project's own; cl.exe searches it already (§3)")
    elif entry == UNIT_TEST_INCLUDE:
      if project.name not in TEST_PROJECTS:
        findings.add(rule, path, f"include directory {entry!r} belongs to test projects only")
    elif not re.fullmatch(r"\$\(SolutionDir\)(" + "|".join(PROJECTS) + r")\\?", entry):
      findings.add(rule, path, f"include directory {entry!r} is not $(SolutionDir)<Project> (§3)")
  out_dir = project.property("OutDir")
  if not out_dir or not out_dir.startswith("$(SolutionDir)"):
    findings.add(rule, path, f"OutDir is {out_dir!r}; it must be anchored on $(SolutionDir) (§3)")
  pch_items = [metadata for include, metadata in project.items.get("ClCompile", []) if include == "pch.cpp"]
  if not pch_items or pch_items[0].get("PrecompiledHeader") != "Create":
    findings.add(rule, path, "pch.cpp must be a ClCompile item with PrecompiledHeader Create")


def check_alignment(project, findings):
  rule = "Alignment"
  path = project.relative
  debug, release = (project.properties[configuration] for configuration in CONFIGURATIONS)
  for name in sorted(set(debug) | set(release)):
    if debug.get(name) != release.get(name) and ("Property", name) not in ALLOWED_TO_DIFFER:
      findings.add(rule, path, f"property {name} is {debug.get(name)!r} in Debug and {release.get(name)!r} in Release")
  debug_items, release_items = (project.definitions[configuration] for configuration in CONFIGURATIONS)
  for item in sorted(set(debug_items) | set(release_items)):
    left, right = debug_items.get(item, {}), release_items.get(item, {})
    for name in sorted(set(left) | set(right)):
      if left.get(name) == right.get(name):
        continue
      if name == "PreprocessorDefinitions":
        difference = set(split_list(left.get(name))) ^ set(split_list(right.get(name)))
        if difference != CONFIGURATION_DEFINES:
          findings.add(rule, path,
                       f"{item}.PreprocessorDefinitions differ by {sorted(difference)}; only _DEBUG/NDEBUG may")
        continue
      if name == "RuntimeLibrary" and {left.get(name), right.get(name)} != RUNTIME_LIBRARY_PAIR:
        findings.add(rule, path, f"{item}.RuntimeLibrary must be the debug and release CRT pair, "
                                 f"not {left.get(name)!r}/{right.get(name)!r}")
        continue
      if (item, name) not in ALLOWED_TO_DIFFER:
        findings.add(rule, path,
                     f"{item}.{name} is {left.get(name)!r} in Debug and {right.get(name)!r} in Release (§3)")


def check_registration(project, findings):
  rule = "Registration"
  listed = project.listed_files()
  listed_normalized = {entry.replace("\\", "/") for entry in listed}
  on_disk = {path.relative_to(project.directory).as_posix() for path in project.on_disk_files()}
  for missing in sorted(on_disk - listed_normalized):
    findings.add(rule, f"{relative(project.directory)}/{missing}", f"on disk but not in {project.path.name}")
  for phantom in sorted(listed_normalized - on_disk):
    findings.add(rule, project.relative, f"lists {phantom!r}, which is not on disk with that spelling")
  filters_normalized = {entry.replace("\\", "/") for entry in project.filter_items}
  for missing in sorted(listed_normalized - filters_normalized):
    findings.add(rule, relative(project.filters_path), f"{missing!r} is in the project but not in the filters")
  for phantom in sorted(filters_normalized - listed_normalized):
    findings.add(rule, relative(project.filters_path), f"lists {phantom!r}, which the project does not")
  # Spelling must be byte-identical: a case-insensitive file system hides a wrong case until CI.
  disk_by_lower = {entry.lower(): entry for entry in on_disk}
  for entry in sorted(listed_normalized):
    if entry not in on_disk and entry.lower() in disk_by_lower:
      findings.add(rule, project.relative, f"{entry!r} is spelled {disk_by_lower[entry.lower()]!r} on disk (R7)")


def check_flat(project, findings):
  for path in sorted(project.directory.rglob("*")):
    if not path.is_dir():
      continue
    parts = path.relative_to(project.directory).parts
    if parts[0] in BUILD_OUTPUT_DIRECTORIES:
      continue
    if len(parts) > 1 or parts[0] not in SANCTIONED_SUBDIRECTORIES:
      findings.add("Flat", relative(path),
                   "a subdirectory of a project; only Shaders/ and CompiledShaders/ are sanctioned (§2)")


def check_r7(project, findings):
  rule = "R7"
  fx_items = {include.replace("\\", "/"): metadata for include, metadata in project.items.get("FXCompile", [])}
  fx_defaults = project.definitions["Debug|x64"].get("FXCompile", {})
  for path in project.on_disk_files():
    name = path.name
    location = relative(path)
    if path.suffix in BANNED_EXTENSIONS:
      findings.add(rule, location, f"{path.suffix} is not used; .h and .cpp only")
      continue
    if path.suffix == ".hlsl":
      match = SHADER_FILE.match(name)
      if not match or path.parent.name != "Shaders":
        findings.add(rule, location, "a shader is Shaders/<Name>VS.hlsl or Shaders/<Name>PS.hlsl (§2)")
        continue
      base, stage = match.groups()
      key = f"Shaders/{name}"
      metadata = {**fx_defaults, **fx_items.get(key, {})}
      if key not in fx_items:
        continue  # Registration reports the missing item
      expected_header = {f"$(ProjectDir)CompiledShaders\\%(Filename).h", f"CompiledShaders\\%(Filename).h",
                         f"$(ProjectDir)CompiledShaders\\{base}{stage}.h", f"CompiledShaders\\{base}{stage}.h"}
      if metadata.get("HeaderFileOutput") not in expected_header:
        findings.add(rule, location, f"FXCompile HeaderFileOutput is {metadata.get('HeaderFileOutput')!r}; "
                                     f"must be CompiledShaders\\{base}{stage}.h (R13)")
      if metadata.get("VariableName") not in {"g_%(Filename)", f"g_{base}{stage}"}:
        findings.add(rule, location,
                     f"FXCompile VariableName is {metadata.get('VariableName')!r}; must be g_{base}{stage} (§2)")
      expected_type = "Vertex" if stage == "VS" else "Pixel"
      if metadata.get("ShaderType") != expected_type:
        findings.add(rule, location,
                     f"FXCompile ShaderType is {metadata.get('ShaderType')!r}; a {stage} shader is {expected_type}")
      if metadata.get("ObjectFileOutput", None) not in ("", None) and metadata.get("ObjectFileOutput"):
        findings.add(rule, location, "FXCompile writes an object file; no .cso on disk (R13)")
      continue
    if name in R7_EXCEPTIONS:
      continue
    if not PASCAL_CASE_FILE.match(name):
      findings.add(rule, location, "a source file is PascalCase.h or PascalCase.cpp (R7)")


def check_unique_names(projects, findings):
  headers = {}
  for project in projects:
    for path in project.on_disk_files():
      if path.suffix == ".h" and path.name not in R7_EXCEPTIONS:
        headers.setdefault(path.name, []).append(relative(path))
  for name, paths in sorted(headers.items()):
    if len(paths) > 1:
      findings.add("UniqueNames", paths[1], f"{name} also exists as {paths[0]}; an include of it would be ambiguous")


def strip_comments_and_strings(text):
  """Replaces comments, string and character literals with spaces, keeping newlines so line numbers survive."""
  out = []
  i = 0
  n = len(text)
  while i < n:
    two = text[i:i + 2]
    if two == "//":
      end = text.find("\n", i)
      end = n if end < 0 else end
      out.append(" " * (end - i))
      i = end
    elif two == "/*":
      end = text.find("*/", i + 2)
      end = n if end < 0 else end + 2
      out.append("".join("\n" if character == "\n" else " " for character in text[i:end]))
      i = end
    elif text[i] == '"' and (i == 0 or text[i - 1] != "'"):
      j = i + 1
      while j < n and text[j] != '"' and text[j] != "\n":
        j += 2 if text[j] == "\\" else 1
      out.append(" " * (min(j + 1, n) - i))
      i = min(j + 1, n)
    elif text[i] == "'":
      j = i + 1
      while j < n and text[j] != "'" and text[j] != "\n":
        j += 2 if text[j] == "\\" else 1
      out.append(" " * (min(j + 1, n) - i))
      i = min(j + 1, n)
    else:
      out.append(text[i])
      i += 1
  return "".join(out)


def line_of(text, offset):
  return text.count("\n", 0, offset) + 1


def check_r2_and_r11(project, findings):
  for path in project.on_disk_files():
    if path.suffix not in SOURCE_EXTENSIONS | {".hlsl"}:
      continue
    text = strip_comments_and_strings(path.read_text(encoding="utf-8", errors="replace"))
    location = relative(path)
    names = [(m.group(1), m.start(1)) for m in TYPE_DECLARATION.finditer(text)]
    names += [(m.group(1), m.start(1)) for m in ALIAS_DECLARATION.finditer(text)]
    for name, offset in names:
      if BANNED_TYPE_PREFIX.match(name) or BANNED_TYPE_SUFFIX.search(name):
        findings.add("R2", location, f"type {name!r} carries a prefix or affix; name the concept (R2)",
                     line_of(text, offset))
    for match in IDENTIFIER.finditer(text):
      lowered = match.group(0).lower()
      for spelling in BANNED_SPELLINGS:
        if spelling in lowered:
          findings.add("R11", location,
                       f"identifier {match.group(0)!r} spells {spelling!r}; the SDK's spelling wins (R11)",
                       line_of(text, match.start()))
          break


def resolve_include(project, including_file, include, projects_by_name):
  """Where `#include "include"` lands, the way cl.exe looks: the including file's directory, then the project's
  include directories in order. Returns (owner project name, path) or (None, None) for an SDK or framework header."""
  candidate = including_file.parent / include
  if candidate.is_file() or include.replace("\\", "/").startswith("CompiledShaders/"):
    return project.name, candidate
  for entry in project.include_directories():
    match = re.fullmatch(r"\$\(SolutionDir\)(" + "|".join(PROJECTS) + r")\\?", entry)
    if not match:
      continue
    owner = projects_by_name[match.group(1)]
    candidate = owner.directory / include
    if candidate.is_file():
      return owner.name, candidate
  return None, None


def check_edges(projects, findings):
  rule = "Edges"
  by_name = {project.name: project for project in projects}
  compiled_shader_includers = {}
  for project in projects:
    allowed = ALLOWED_DEPENDENCIES[project.name] | {project.name}
    for entry in project.include_directories():
      match = re.fullmatch(r"\$\(SolutionDir\)(" + "|".join(PROJECTS) + r")\\?", entry)
      if match and match.group(1) not in allowed:
        findings.add(rule, project.relative,
                     f"puts {match.group(1)} on its include path, which the project graph does not allow (§2)")
    for reference in project.references:
      name = pathlib.PureWindowsPath(reference).stem
      if name not in allowed:
        findings.add(rule, project.relative, f"references {name}, which the project graph does not allow (§2)")
    for path in project.on_disk_files():
      if path.suffix not in SOURCE_EXTENSIONS:
        continue
      text = path.read_text(encoding="utf-8", errors="replace")
      for match in QUOTED_INCLUDE.finditer(text):
        include = match.group(1)
        owner, target = resolve_include(project, path, include, by_name)
        line = line_of(text, match.start())
        if owner is None:
          continue
        location = relative(path)
        if include.replace("\\", "/").startswith("CompiledShaders/"):
          key = f"{project.name}/{include}"
          compiled_shader_includers.setdefault(key, []).append((location, line))
          continue
        if owner not in allowed:
          findings.add(rule, location,
                       f'includes "{include}" from {owner}; '
                       f'{project.name} may include only from {sorted(allowed)} (§2)', line)
          continue
        target_name = target.name
        client_side = project.name == "NomadCommander" and path.name != COMPOSITION_ROOT
        if owner == "GameLogic" and client_side and not WIRE_HEADER.match(target_name):
          findings.add(rule, location,
                       f'includes "{include}" from GameLogic; a client-side file sees GameLogic only through Wire*.h '
                       "(ADR-001, R18)", line)
        if WIRE_HEADER.match(path.name) and owner == "GameLogic" and not WIRE_HEADER.match(target_name):
          findings.add(rule, location,
                       f'a Wire header includes "{include}"; Wire*.h includes only NeuronCore and other Wire headers '
                       "(ADR-001)", line)
  for key, includers in sorted(compiled_shader_includers.items()):
    if len(includers) > 1:
      for location, line in includers[1:]:
        findings.add(rule, location,
                     f"{key} is included here and in {includers[0][0]}; one .cpp binds a compiled shader (§2)", line)


def check_compiled_shaders_not_committed(findings):
  try:
    output = subprocess.run(["git", "ls-files", "--", "*/CompiledShaders/*"], cwd=ROOT, capture_output=True, text=True,
                            check=True).stdout
  except (OSError, subprocess.CalledProcessError):
    print("CheckProjectFiles: warning: git is not available; the CompiledShaders/ commit check was skipped")
    return
  for line in output.splitlines():
    findings.add("Registration", line.strip(), "committed under CompiledShaders/, which is build output (§2)")


def check_solution(findings):
  rule = "Solution"
  path = ROOT / "NomadCommander.slnx"
  if not path.is_file():
    findings.add(rule, "NomadCommander.slnx", "the solution file is missing")
    return
  try:
    root = ElementTree.parse(path).getroot()
  except ElementTree.ParseError as error:
    findings.add(rule, "NomadCommander.slnx", f"not well-formed XML: {error}")
    return
  listed = {element.get("Path", "").replace("\\", "/") for element in root.iter("Project")}
  expected = {f"{directory}/{name}.vcxproj" for name, directory in PROJECTS.items()}
  for missing in sorted(expected - listed):
    findings.add(rule, "NomadCommander.slnx", f"does not list {missing}")
  for extra in sorted(listed - expected):
    findings.add(rule, "NomadCommander.slnx", f"lists {extra}, which is not one of the nine projects (§2)")
  platforms = {element.get("Name") for element in root.iter("Platform")}
  if platforms != {"x64"}:
    findings.add(rule, "NomadCommander.slnx", f"platforms are {sorted(platforms)}; x64 is the only platform (§3)")
  build_types = {element.get("Name") for element in root.iter("BuildType")}
  if build_types != {"Debug", "Release"}:
    findings.add(rule, "NomadCommander.slnx", f"build types are {sorted(build_types)}; Debug and Release only")


def list_rules():
  in_rules = False
  for line in __doc__.splitlines():
    if line.startswith("The ten rules"):
      in_rules = True
      continue
    if in_rules and line.strip():
      print(line)


def main():
  parser = argparse.ArgumentParser(description="Check the build shape, the project registries and the naming rules.")
  parser.add_argument("--list-rules", action="store_true", help="print the ten rules and exit")
  arguments = parser.parse_args()
  if arguments.list_rules:
    list_rules()
    return 0
  findings = Findings()
  projects = [Project(name, findings) for name in PROJECTS]
  for project in projects:
    if not project.loaded:
      continue
    check_shape(project, findings)
    check_alignment(project, findings)
    check_registration(project, findings)
    check_flat(project, findings)
    check_r7(project, findings)
    check_r2_and_r11(project, findings)
  loaded = [project for project in projects if project.loaded]
  check_unique_names(loaded, findings)
  check_edges(loaded, findings)
  check_compiled_shaders_not_committed(findings)
  check_solution(findings)
  if findings.lines:
    for line in findings.lines:
      print(line)
    print(f"CheckProjectFiles: {len(findings.lines)} finding(s)", file=sys.stderr)
    return 1
  print(f"CheckProjectFiles: {len(loaded)} projects, clean")
  return 0


if __name__ == "__main__":
  sys.exit(main())
