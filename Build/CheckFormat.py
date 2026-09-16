#!/usr/bin/env python3
"""Build/CheckFormat.py: the whole-tree clang-format gate (AGENTS.md §3, §4, §6).

Runs clang-format over every hand-written C++ file in the tree and exits 1 when any would change; --fix rewrites the
offenders in place. Nothing beyond the Python standard library and a clang-format binary.

CI runs this in its own Linux job on clang-format 18.1.3 exactly, because the output changes between releases: 18 and
22 break a long argument list in different places, and a floating version would flap. The script prints the version
it used and warns when the major version is not the pinned one; the Linux job is the answer that counts.

Line endings survive: .clang-format leaves LineEnding at its default (DeriveLF), so a CRLF file comes back CRLF from a
Windows checkout and an LF file comes back LF from a Linux container. The comparison below is byte for byte, which is
what makes that a checkable claim rather than a hope.
"""
import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PROJECT_DIRECTORIES = ["NeuronCore", "NeuronClient", "NeuronServer", "GameLogic", "NomadCommander"]
TESTS_DIRECTORY = "Tests"
SKIPPED_DIRECTORY_NAMES = {"CompiledShaders", "x64", ".vs"}
SKIPPED_FILE_NAMES = {"Resource.h"}  # written by the Visual Studio resource editor, not by hand (.clang-tidy)
EXTENSIONS = {".cpp", ".h"}
PINNED_MAJOR_VERSION = 18


def collect_files():
  """Every .cpp and .h under the five project directories and Tests/*Tests, skipping build output."""
  roots = [ROOT / name for name in PROJECT_DIRECTORIES]
  tests = ROOT / TESTS_DIRECTORY
  if tests.is_dir():
    roots += sorted(path for path in tests.iterdir() if path.is_dir() and path.name.endswith("Tests"))
  files = []
  for root in roots:
    if not root.is_dir():
      continue
    for path in sorted(root.rglob("*")):
      if any(part in SKIPPED_DIRECTORY_NAMES for part in path.relative_to(root).parts):
        continue
      if path.is_file() and path.suffix in EXTENSIONS and path.name not in SKIPPED_FILE_NAMES:
        files.append(path)
  return files


def clang_format_version(binary):
  """Returns (version string, major) or exits 2 when the binary cannot be run."""
  try:
    output = subprocess.run([binary, "--version"], capture_output=True, text=True, check=True).stdout.strip()
  except (OSError, subprocess.CalledProcessError) as error:
    print(f"CheckFormat: cannot run {binary!r}: {error}", file=sys.stderr)
    sys.exit(2)
  match = re.search(r"version\s+(\d+)\.(\d+)\.(\d+)", output)
  major = int(match.group(1)) if match else 0
  return output, major


def format_file(binary, path):
  """Returns (path, original bytes, formatted bytes). The style comes from .clang-format, found from the path."""
  original = path.read_bytes()
  result = subprocess.run([binary, "--style=file", str(path)], capture_output=True, check=False)
  if result.returncode != 0:
    message = result.stderr.decode("utf-8", errors="replace").strip()
    raise RuntimeError(f"{path.relative_to(ROOT)}: clang-format failed: {message}")
  return path, original, result.stdout


def main():
  parser = argparse.ArgumentParser(description="Check (or, with --fix, apply) clang-format over the whole tree.")
  parser.add_argument("--clang-format", default="clang-format", help="the binary to run (default: clang-format)")
  parser.add_argument("--fix", action="store_true", help="rewrite the files that are not formatted")
  parser.add_argument("--verbose", action="store_true", help="list every file checked")
  arguments = parser.parse_args()

  version, major = clang_format_version(arguments.clang_format)
  print(f"CheckFormat: {version}")
  if major != PINNED_MAJOR_VERSION:
    print(f"CheckFormat: warning: CI pins clang-format {PINNED_MAJOR_VERSION}; a disagreement with CI is a version "
          "difference before it is a formatting one")

  files = collect_files()
  if not files:
    print("CheckFormat: no C++ files found", file=sys.stderr)
    return 2

  offenders = []
  with concurrent.futures.ThreadPoolExecutor() as pool:
    try:
      results = list(pool.map(lambda path: format_file(arguments.clang_format, path), files))
    except RuntimeError as error:
      print(f"CheckFormat: {error}", file=sys.stderr)
      return 2
  for path, original, formatted in results:
    relative = path.relative_to(ROOT).as_posix()
    if original == formatted:
      if arguments.verbose:
        print(f"  ok       {relative}")
      continue
    offenders.append(path)
    if arguments.fix:
      path.write_bytes(formatted)
      print(f"  fixed    {relative}")
    else:
      print(f"{relative}: not formatted")

  checked = len(files)
  if not offenders:
    print(f"CheckFormat: {checked} files formatted")
    return 0
  if arguments.fix:
    print(f"CheckFormat: rewrote {len(offenders)} of {checked} files")
    return 0
  print(f"CheckFormat: {len(offenders)} of {checked} files would change; run with --fix", file=sys.stderr)
  return 1


if __name__ == "__main__":
  sys.exit(main())
