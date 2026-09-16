// NeuronClient/Font.h
#pragma once

#include "FontData.h"

#include <cstdint>

namespace Neuron
{

/// The desk's text faces (ADR-016): IBM Plex Mono, rasterized once by Tools/BakeFont.py and compiled in as coverage
/// through FontData.h and FontCoverage.h (AGENTS.md R13). A face is named for its ROLE rather than its size or
/// weight, so a screen says what a line is and the bake decides what that looks like: re-baking Title at another
/// weight touches no call site.
///
/// Every face is FONT_LINE_HEIGHT_PIXELS tall, which is the cell row of UI §1, and advances by a whole number of
/// pixels a character. The pipeline reads the coverage with Load and blends the text colour by it, so a glyph reaches
/// the glass exactly as the rasterizer left it and nothing is scaled.
enum class Font : std::uint8_t
{
  Body,  ///< Regular at 20 px, 12 px a character: reports, rows, labels, buttons — two characters to a cell
  Small, ///< Regular at 16 2/3 px, 10 px a character: the line under a name, a track record, a legend
  Title  ///< SemiBold at 20 px, 12 px a character: a panel's title, the selected item's name
};

inline constexpr std::uint32_t FONT_COUNT = 3;

/// What a face is made of: how far a character advances, which row of the cell its baseline sits on, and the
/// coverage it was baked to — FONT_GLYPH_COUNT glyphs of advance × FONT_LINE_HEIGHT_PIXELS bytes each, row-major.
/// A public aggregate, so plain fields (R8).
struct FontMetrics
{
  std::uint32_t advancePixels;
  std::uint32_t baselinePixels;
  const std::uint8_t* coverage;
};

/// The metrics of a face. Defined in Font.cpp, the one translation unit that includes the coverage bytes.
[[nodiscard]] FontMetrics MetricsOf(Font _font) noexcept;

} // namespace Neuron
