// NeuronClient/Font.cpp
#include "pch.h"
#include "Font.h"
// The coverage bytes, and the one place they are included: 78 KB of initializer that every unit seeing it would pay
// to parse (Tools/BakeFont.py).
#include "FontCoverage.h"

namespace Neuron
{

FontMetrics MetricsOf(Font _font) noexcept
{
  switch (_font)
  {
  case Font::Small:
    return FontMetrics{FONT_SMALL_ADVANCE_PIXELS, FONT_SMALL_BASELINE_PIXELS, FONT_SMALL_COVERAGE.data()};
  case Font::Title:
    return FontMetrics{FONT_TITLE_ADVANCE_PIXELS, FONT_TITLE_BASELINE_PIXELS, FONT_TITLE_COVERAGE.data()};
  case Font::Body:
  default:
    return FontMetrics{FONT_BODY_ADVANCE_PIXELS, FONT_BODY_BASELINE_PIXELS, FONT_BODY_COVERAGE.data()};
  }
}

} // namespace Neuron
