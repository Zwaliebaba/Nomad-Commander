#!/usr/bin/env python3
"""Build/RunClangTidy.py: clang-tidy over every hand-written translation unit, through clang's MSVC driver.

AGENTS.md §1 (the naming table and what .clang-tidy enforces), §3 (run the checkers), §6 (the CI table). It is the
last step of the Windows CI job, after the build, so that a compile error is reported as a compile error rather than
as forty findings about a file that does not parse, and so that CompiledShaders/ exists.

Each file runs as

  clang-tidy --quiet <file> -- --driver-mode=cl /std:c++latest /EHsc /DUNICODE /D_UNICODE /D<defines> /I<dirs>

with the defines and include directories read from the file's .vcxproj (Debug|x64, the configuration CI builds) by
CheckProjectFiles.py's parser, so the linter and the build cannot disagree about them. UNICODE and _UNICODE come from
CharacterSet=Unicode, which every project sets; without them clang reports a type error at every LoadCursorW call that
MSVC compiles without complaint. The Windows macro family is not passed: NeuronCore.h owns it (AGENTS.md §4), and a
/D copy would be a macro redefinition that WarningsAsErrors turns fatal.

The Windows SDK and the CRT become visible to clang through the INCLUDE environment variable, which is what a
Developer PowerShell (or the workflow's "Import the MSVC environment" step) sets. VCINSTALLDIR, from the same source,
expands $(VCInstallDir) for the test projects' CppUnitTest include directory.

The version matters, and CI pins it: CLANG_TIDY_VERSION in .github/workflows/build.yml, installed from pip so the same
binary runs anywhere (`python -m pip install clang-tidy==<that>`). This script reads the pin from the workflow and warns
when the binary on the path is something else.
"""
import argparse
import concurrent.futures
import os
import pathlib
import re
import subprocess
import sys

BUILD_DIRECTORY = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(BUILD_DIRECTORY))
import CheckProjectFiles as Registry  # noqa: E402  (the shared .vcxproj parser)

ROOT = Registry.ROOT
WORKFLOW = ROOT / ".github" / "workflows" / "build.yml"
CONFIGURATION = "Debug|x64"
FIXED_SWITCHES = ["--driver-mode=cl", "/std:c++latest", "/EHsc", "/DUNICODE", "/D_UNICODE"]
SKIPPED_FILE_NAMES = {"pch.cpp"}  # it includes pch.h and nothing else; the headers are checked through every other unit
COMPILED_SHADER_INCLUDE = re.compile(r'^\s*#\s*include\s+"(CompiledShaders[/\\][^"]+)"', re.MULTILINE)


def pinned_version():
  """The CLANG_TIDY_VERSION the workflow installs, or None when the workflow does not say."""
  try:
    match = re.search(r"^\s*CLANG_TIDY_VERSION:\s*([0-9][0-9.]*)", WORKFLOW.read_text(encoding="utf-8"), re.MULTILINE)
  except OSError:
    return None
  return match.group(1) if match else None


def clang_tidy_version(binary):
  try:
    output = subprocess.run([binary, "--version"], capture_output=True, text=True, check=True).stdout
  except (OSError, subprocess.CalledProcessError) as error:
    print(f"RunClangTidy: cannot run {binary!r}: {error}", file=sys.stderr)
    sys.exit(2)
  match = re.search(r"version\s+(\d+\.\d+\.\d+)", output)
  return match.group(1) if match else output.strip()


def expand_msbuild(value, project):
  """The three MSBuild properties this tree's include directories use, and nothing else."""
  vc_install_dir = os.environ.get("VCINSTALLDIR")
  if "$(VCInstallDir)" in value:
    if not vc_install_dir:
      raise RuntimeError("VCINSTALLDIR is not set; run from a Developer PowerShell (VsDevCmd.bat -arch=amd64)")
    value = value.replace("$(VCInstallDir)", vc_install_dir.rstrip("\\") + "\\")
  value = value.replace("$(SolutionDir)", str(ROOT) + os.sep)
  value = value.replace("$(ProjectDir)", str(project.directory) + os.sep)
  if "$(" in value:
    raise RuntimeError(f"unexpanded MSBuild property in {value!r}")
  return value


def collect_units(projects):
  """Every .cpp under the nine projects except pch.cpp and build output, with the project it belongs to."""
  units = []
  for project in projects:
    for path in project.on_disk_files():
      if path.suffix == ".cpp" and path.name not in SKIPPED_FILE_NAMES:
        units.append((path, project))
  return units


def project_switches(project):
  switches = list(FIXED_SWITCHES)
  for define in Registry.split_list(project.metadata("ClCompile", "PreprocessorDefinitions", CONFIGURATION)):
    switches.append(f"/D{define}")
  for directory in project.include_directories():
    switches.append("/I" + expand_msbuild(directory, project))
  return switches


def missing_compiled_shaders(path):
  text = path.read_text(encoding="utf-8", errors="replace")
  return [include for include in COMPILED_SHADER_INCLUDE.findall(text) if not (path.parent / include).is_file()]


def run_one(binary, path, switches, dry_run):
  command = [binary, "--quiet", str(path.relative_to(ROOT)), "--"] + switches
  if dry_run:
    return path, 0, " ".join(command)
  result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, check=False)
  output = (result.stdout + result.stderr).strip()
  return path, result.returncode, output


def main():
  parser = argparse.ArgumentParser(description="Run clang-tidy over every hand-written translation unit.")
  parser.add_argument("--clang-tidy", default="clang-tidy", help="the binary to run (default: clang-tidy on the path)")
  parser.add_argument("--jobs", type=int, default=os.cpu_count() or 1, help="parallel clang-tidy processes")
  parser.add_argument("--files", nargs="+", help="check only these translation units (paths relative to the root)")
  parser.add_argument("--verbose", action="store_true", help="print each command and each clean file")
  parser.add_argument("--dry-run", action="store_true", help="print the commands and run nothing")
  arguments = parser.parse_args()

  if not arguments.dry_run:
    if not os.environ.get("INCLUDE"):
      print("RunClangTidy: INCLUDE is not set, so clang cannot see the CRT or the Windows SDK; run from a Developer "
            "PowerShell (VsDevCmd.bat -arch=amd64 -host_arch=amd64)", file=sys.stderr)
      return 2
    version = clang_tidy_version(arguments.clang_tidy)
    pin = pinned_version()
    print(f"RunClangTidy: clang-tidy {version}" + (f" (CI pins {pin})" if pin else ""))
    if pin and version != pin:
      print(f"RunClangTidy: warning: CI runs {pin}; install it with `python -m pip install clang-tidy=={pin}` before "
            "trusting a disagreement")

  findings = Registry.Findings()
  projects = [project for project in (Registry.Project(name, findings) for name in Registry.PROJECTS) if project.loaded]
  units = collect_units(projects)
  if arguments.files:
    wanted = {pathlib.Path(name).resolve() for name in arguments.files}
    units = [(path, project) for path, project in units if path.resolve() in wanted]
    if len(units) != len(wanted):
      print("RunClangTidy: some of --files are not translation units of a project", file=sys.stderr)
      return 2
  if not units:
    print("RunClangTidy: no translation units found", file=sys.stderr)
    return 2

  failures = 0
  for path, _ in units:
    for include in missing_compiled_shaders(path):
      print(f"{path.relative_to(ROOT).as_posix()}: includes {include}, which the build has not produced yet; build "
            "Debug|x64 first (AGENTS.md §2: CompiledShaders/ is build output)")
      failures += 1
  if failures and not arguments.dry_run:
    return 1

  try:
    switches_by_project = {project.name: project_switches(project) for _, project in units}
  except RuntimeError as error:
    print(f"RunClangTidy: {error}", file=sys.stderr)
    return 2

  with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, arguments.jobs)) as pool:
    futures = [pool.submit(run_one, arguments.clang_tidy, path, switches_by_project[project.name], arguments.dry_run)
               for path, project in units]
    results = [future.result() for future in futures]
  for path, code, output in sorted(results, key=lambda result: str(result[0])):
    relative = path.relative_to(ROOT).as_posix()
    if arguments.dry_run:
      print(output)
      continue
    if code != 0:
      failures += 1
      print(f"--- {relative}")
      print(output)
    elif arguments.verbose:
      print(f"  clean    {relative}")
  if arguments.dry_run:
    print(f"RunClangTidy: {len(units)} commands, none run")
    return 0
  if failures:
    print(f"RunClangTidy: {failures} of {len(units)} translation units have findings", file=sys.stderr)
    return 1
  print(f"RunClangTidy: {len(units)} translation units clean")
  return 0


if __name__ == "__main__":
  sys.exit(main())
