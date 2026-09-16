#!/usr/bin/env python3
"""Tools/BakeFont.py: rasterize the desk's text faces into NeuronClient/FontData.h and FontCoverage.h (ADR-016).

The client draws text from coverage baked into the executable, never from a font file at runtime (AGENTS.md R13).
This tool is where the baking happens, once, by hand, and its output is committed: the build needs nothing but the
two headers, and CI needs neither this script nor Pillow. Run it again only when the faces change.

    py Tools\\BakeFont.py --regular <IBMPlexMono-Regular.ttf> --semibold <IBMPlexMono-SemiBold.ttf> --license <OFL.txt>

The three faces (Font.h): Body is Regular at 20 px, Small is Regular at 16 2/3 px, Title is SemiBold at 20 px. Every
face is 24 px tall, the cell row of UI section 1, and a character advances by a whole number of pixels (12, 10, 12):
IBM Plex Mono's advance is 600/1000 em, which is why the sizes are what they are. Each glyph is rendered by FreeType
through Pillow with hinting and 8-bit anti-aliasing, at its own pen position with the font's own side bearings, into a
cell of advance x 24 texels; 0x7F is a hollow box drawn here, the glyph the renderer shows for a character the set
does not hold. What comes out is a byte per texel, 0 to 255, which the glyph pipeline reads with Load and blends by.

Two headers, because the coverage is 78 KB of initializer and MSVC pays for it in every translation unit that sees
it: FontData.h holds the metrics (advance, baseline, sizes) and is cheap to include anywhere; FontCoverage.h holds the
bytes and is included by Font.cpp alone.

Provenance is written into both: the source files' SHA-256, the font's version string from its name table, the Pillow
and FreeType versions, and (in FontCoverage.h, beside the bytes) the whole licence text. IBM Plex Mono is under the
SIL Open Font License 1.1 with the reserved font name "Plex"; a baked atlas is a modified version under that licence,
so it carries the text and is not itself called Plex.

The output depends on the rasterizer: a different Pillow or FreeType may round a stem differently, and a regeneration
then shows as a diff. That is fine -- the headers are the artifact, reviewed like any other -- and it is why the
versions are recorded beside the bytes.
"""
import argparse
import datetime
import hashlib
import pathlib
import struct
import sys

try:
  import PIL
  from PIL import Image, ImageDraw, ImageFont, features
except ImportError:  # pragma: no cover
  print("BakeFont: Pillow is needed to rasterize (`py -m pip install pillow`); the committed headers need nothing",
        file=sys.stderr)
  sys.exit(2)

ROOT = pathlib.Path(__file__).resolve().parent.parent
DATA_OUTPUT = ROOT / "NeuronClient" / "FontData.h"
COVERAGE_OUTPUT = ROOT / "NeuronClient" / "FontCoverage.h"

FIRST_CODEPOINT = 0x20
GLYPH_COUNT = 96  # 0x20 to 0x7F; 0x7F is the replacement box
REPLACEMENT_CODEPOINT = 0x7F
LINE_HEIGHT_PIXELS = 24  # the cell row (UI section 1); every face is baked to it

# name, which file, pixel size, advance in pixels. The advance is 0.6 em, so 20 px gives 12 and 50/3 px gives 10.
FACES = [
  ("BODY", "regular", 20.0, 12),
  ("SMALL", "regular", 50.0 / 3.0, 10),
  ("TITLE", "semibold", 20.0, 12),
]


def sha256(path):
  return hashlib.sha256(path.read_bytes()).hexdigest()


def name_table_entry(path, wanted_id):
  """A string from the TrueType name table (Windows platform, first match), or None."""
  data = path.read_bytes()
  table_count = struct.unpack(">H", data[4:6])[0]
  for index in range(table_count):
    record = 12 + 16 * index
    tag, _checksum, offset, _length = struct.unpack(">4sIII", data[record:record + 16])
    if tag != b"name":
      continue
    _format, count, string_offset = struct.unpack(">HHH", data[offset:offset + 6])
    storage = offset + string_offset
    for entry in range(count):
      at = offset + 6 + 12 * entry
      platform, _encoding, _language, name_id, length, start = struct.unpack(">HHHHHH", data[at:at + 12])
      if name_id == wanted_id and platform == 3:
        return data[storage + start:storage + start + length].decode("utf-16-be", errors="replace")
  return None


def bake_face(font, advance):
  """Renders the 96 cells of one face. Returns (list of bytes objects, metrics dict)."""
  ascent = descent = 0
  for codepoint in range(FIRST_CODEPOINT, REPLACEMENT_CODEPOINT):
    _left, top, _right, bottom = font.getbbox(chr(codepoint), anchor="ls")
    ascent = max(ascent, -top)
    descent = max(descent, bottom)
  cap_height = -font.getbbox("H", anchor="ls")[1]
  x_height = -font.getbbox("x", anchor="ls")[1]
  if ascent + descent > LINE_HEIGHT_PIXELS:
    raise SystemExit(f"BakeFont: the face needs {ascent + descent} rows and the cell has {LINE_HEIGHT_PIXELS}")
  # The baseline: capitals centred in the cell, pulled up only if a descender would otherwise leave it, and never so
  # high that the tallest glyph leaves it at the top.
  baseline = min((LINE_HEIGHT_PIXELS + cap_height) // 2, LINE_HEIGHT_PIXELS - descent)
  baseline = max(baseline, ascent)

  glyphs = []
  clipped = []
  for codepoint in range(FIRST_CODEPOINT, FIRST_CODEPOINT + GLYPH_COUNT):
    # A canvas three cells wide and two tall, the cell in the middle, so ink that leaves the cell is counted rather
    # than lost.
    canvas = Image.new("L", (advance * 3, LINE_HEIGHT_PIXELS * 2), 0)
    draw = ImageDraw.Draw(canvas)
    origin_x, origin_y = advance, LINE_HEIGHT_PIXELS // 2
    if codepoint == REPLACEMENT_CODEPOINT:
      draw.rectangle([origin_x + 1, origin_y + baseline - cap_height, origin_x + advance - 2, origin_y + baseline - 1],
                     outline=255)
    else:
      draw.text((origin_x, origin_y + baseline), chr(codepoint), font=font, fill=255, anchor="ls")
    cell = canvas.crop((origin_x, origin_y, origin_x + advance, origin_y + LINE_HEIGHT_PIXELS))
    if sum(canvas.tobytes()) != sum(cell.tobytes()):
      clipped.append(chr(codepoint))
    glyphs.append(cell.tobytes())
  metrics = {"baseline": baseline, "ascent": ascent, "descent": descent, "cap_height": cap_height,
             "x_height": x_height, "clipped": clipped}
  return glyphs, metrics


def comment_block(text):
  return "\n".join(("// " + line).rstrip() for line in text.replace("\r\n", "\n").split("\n"))


def preamble(name, what, provenance):
  out = [f"// NeuronClient/{name}", "//",
         "// GENERATED by Tools/BakeFont.py -- do not edit by hand; edit the tool or its inputs and run it again.", "//"]
  out += ["// " + line for line in what]
  out.append("//")
  out += [("// " + line).rstrip() for line in provenance]
  return out


def emit_data(faces, provenance):
  out = preamble("FontData.h", [
    "The metrics of the desk's text faces (ADR-016): the cell row every face is baked to, each face's advance and",
    "baseline, and the size of its coverage. Cheap to include anywhere; the bytes themselves are in FontCoverage.h.",
  ], provenance)
  out += ["#pragma once", "", "#include <cstddef>", "#include <cstdint>", "", "namespace Neuron", "{", ""]
  out.append(f"inline constexpr std::uint32_t FONT_FIRST_CODEPOINT = 0x{FIRST_CODEPOINT:02X};")
  out.append(f"inline constexpr std::uint32_t FONT_GLYPH_COUNT = {GLYPH_COUNT};")
  out.append(f"inline constexpr std::uint32_t FONT_LINE_HEIGHT_PIXELS = {LINE_HEIGHT_PIXELS};")
  for name, _glyphs, advance, metrics in faces:
    out.append("")
    out.append(f"inline constexpr std::uint32_t FONT_{name}_ADVANCE_PIXELS = {advance};")
    out.append(f"inline constexpr std::uint32_t FONT_{name}_BASELINE_PIXELS = {metrics['baseline']};")
    # Broken here rather than by clang-format later, so that re-running this tool is a no-op against CheckFormat.py.
    out.append(f"inline constexpr std::size_t FONT_{name}_BYTES =")
    out.append(f"  static_cast<std::size_t>(FONT_GLYPH_COUNT) * FONT_{name}_ADVANCE_PIXELS * FONT_LINE_HEIGHT_PIXELS;")
  out += ["", "} // namespace Neuron", ""]
  return "\n".join(out)


def emit_coverage(faces, provenance, license_text):
  out = preamble("FontCoverage.h", [
    "The desk's text faces as coverage, one byte a texel (ADR-016). Each face holds 96 glyphs, ASCII 0x20 to 0x7F, in",
    "codepoint order; a glyph is advance x line-height texels, row-major, top row first; 0x7F is the box the renderer",
    "draws for a character the set does not hold. Baked from IBM Plex Mono (SIL Open Font License 1.1, reserved font",
    "name \"Plex\"); this atlas is a modified version under that licence, carries its text below, and is not itself",
    "called Plex. AGENTS.md R13: compiled in, nothing to load. Included by Font.cpp and nothing else, because 78 KB of",
    "initializer is paid for by every translation unit that sees it.",
  ], provenance)
  out += ["//", "// ---- The licence, verbatim from the OFL.txt distributed with the font ----", "//",
          comment_block(license_text), "#pragma once", "", "#include \"FontData.h\"", "", "#include <array>",
          "#include <cstdint>", "", "namespace Neuron", "{"]
  for name, glyphs, advance, _metrics in faces:
    out.append("")
    out.append("// clang-format off")
    out.append(f"inline constexpr std::array<std::uint8_t, FONT_{name}_BYTES> FONT_{name}_COVERAGE = {{")
    for index, glyph in enumerate(glyphs):
      codepoint = FIRST_CODEPOINT + index
      label = "replacement box" if codepoint == REPLACEMENT_CODEPOINT else repr(chr(codepoint))
      out.append(f"  // 0x{codepoint:02X} {label}")
      for row in range(LINE_HEIGHT_PIXELS):
        values = glyph[row * advance:(row + 1) * advance]
        out.append("  " + ",".join(str(value) for value in values) + ",")
    out.append("};")
    out.append("// clang-format on")
  out += ["", "} // namespace Neuron", ""]
  return "\n".join(out)


def write_preview(faces, path):
  """A picture of each face at 1:1 on the desk's background, in the desk's text colour, for a person to look at."""
  sample = "Supplying the Varn siege of Kessel. Target of the Oren offer: 4 haulers, 2 raiders (9 h) -> 5-9 h"
  width = max(len(sample) * advance for _name, _glyphs, advance, _metrics in faces)
  height = (LINE_HEIGHT_PIXELS + 8) * len(faces) + 8
  background = (0x0B, 0x0E, 0x13)
  ink = (0xE6, 0xE1, 0xD6)
  image = Image.new("RGB", (width, height), background)
  pixels = image.load()
  y_offset = 8
  for _name, glyphs, advance, _metrics in faces:
    for column, character in enumerate(sample):
      index = ord(character) - FIRST_CODEPOINT if FIRST_CODEPOINT <= ord(character) < 0x80 else GLYPH_COUNT - 1
      glyph = glyphs[index]
      for row in range(LINE_HEIGHT_PIXELS):
        for x in range(advance):
          coverage = glyph[row * advance + x]
          if coverage:
            blended = tuple((ink[c] * coverage + background[c] * (255 - coverage) + 127) // 255 for c in range(3))
            pixels[column * advance + x, y_offset + row] = blended
    y_offset += LINE_HEIGHT_PIXELS + 8
  image.save(path)


def write_header(path, text):
  path.write_bytes(text.replace("\n", "\r\n").encode("utf-8"))


def main():
  parser = argparse.ArgumentParser(description="Bake the desk's text faces into NeuronClient/FontData.h and FontCoverage.h.")
  parser.add_argument("--regular", required=True, type=pathlib.Path, help="IBMPlexMono-Regular.ttf")
  parser.add_argument("--semibold", required=True, type=pathlib.Path, help="IBMPlexMono-SemiBold.ttf")
  parser.add_argument("--license", required=True, type=pathlib.Path, help="the OFL.txt distributed with the font")
  parser.add_argument("--preview", type=pathlib.Path, help="also write a PNG of each face at 1:1, for looking at")
  arguments = parser.parse_args()

  files = {"regular": arguments.regular, "semibold": arguments.semibold}
  for key, path in files.items():
    if not path.is_file():
      print(f"BakeFont: {key} font {path} does not exist", file=sys.stderr)
      return 2
  license_text = arguments.license.read_text(encoding="utf-8")

  faces = []
  for name, key, size, advance in FACES:
    font = ImageFont.truetype(str(files[key]), size, layout_engine=ImageFont.Layout.BASIC)
    glyphs, metrics = bake_face(font, advance)
    faces.append((name, glyphs, advance, metrics))
    print(f"BakeFont: {name:<5} {files[key].name} at {size:.4g} px: advance {advance}, baseline {metrics['baseline']}, "
          f"ascent {metrics['ascent']}, descent {metrics['descent']}, cap height {metrics['cap_height']}, "
          f"x-height {metrics['x_height']}" + (f", CLIPPED {metrics['clipped']}" if metrics["clipped"] else ""))

  provenance = [
    f"Baked {datetime.date.today().isoformat()} by Tools/BakeFont.py with Pillow {PIL.__version__} on FreeType "
    f"{features.version('freetype2')}, Python {sys.version.split()[0]}.",
    "",
    "Sources (https://github.com/google/fonts/tree/main/ofl/ibmplexmono):",
  ]
  for key, path in files.items():
    version = name_table_entry(path, 5) or "version unknown"
    provenance.append(f"  {path.name}: {version}; SHA-256 {sha256(path)}")
  provenance.append(f"  {arguments.license.name}: SHA-256 {sha256(arguments.license)}")
  provenance.append("")
  provenance.append("Faces (Font.h): " + "; ".join(
    f"{name} = {files[key].stem.split('-')[-1]} {size:.4g} px, {advance} px advance" for name, key, size, advance in FACES))
  provenance.append(f"Every face is {LINE_HEIGHT_PIXELS} px tall, which is the cell row (UI section 1).")

  write_header(DATA_OUTPUT, emit_data(faces, provenance))
  write_header(COVERAGE_OUTPUT, emit_coverage(faces, provenance, license_text))
  total = sum(len(glyph) for _name, glyphs, _advance, _metrics in faces for glyph in glyphs)
  print(f"BakeFont: wrote {DATA_OUTPUT.relative_to(ROOT).as_posix()} ({DATA_OUTPUT.stat().st_size} bytes) and "
        f"{COVERAGE_OUTPUT.relative_to(ROOT).as_posix()} ({COVERAGE_OUTPUT.stat().st_size} bytes, "
        f"{total} coverage bytes)")
  if arguments.preview:
    write_preview(faces, arguments.preview)
    print(f"BakeFont: preview at {arguments.preview}")
  return 0


if __name__ == "__main__":
  sys.exit(main())
