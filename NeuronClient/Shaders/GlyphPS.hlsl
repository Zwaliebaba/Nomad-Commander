// NeuronClient/Shaders/GlyphPS.hlsl
//
// The pixel half of the text pass. Compiled into CompiledShaders/GlyphPS.h as g_GlyphPS.
//
// Texture2D<float> and Load, with no sampler anywhere: Load takes integer texel coordinates and has no filtering to
// switch on, so a glyph reaches the glass exactly as Tools/BakeFont.py rasterized it (AGENTS.md §5, ADR-016). What
// the texel holds is coverage, 0 to 1, and it becomes the pixel's alpha: the pipeline blends the text colour over
// whatever is under it by that much, which is what anti-aliased type is. A texel with no coverage is discarded rather
// than blended at zero, which is cheaper and leaves the pixel untouched.
Texture2D<float> g_glyphAtlas : register(t0);

float4 main(float4 _position : SV_Position, float2 _texel : TEXCOORD0, nointerpolation uint _colorRgba : COLOR0) : SV_Target
{
  // Truncation, not rounding: the interpolated coordinate runs from the texel's left edge to its right, so the one
  // screen pixel a texel covers floors to that texel.
  const int2 texel = int2(_texel);
  const float coverage = g_glyphAtlas.Load(int3(texel, 0));
  if (coverage <= 0.0f)
  {
    discard;
  }
  const uint3 channels = uint3(_colorRgba, _colorRgba >> 8, _colorRgba >> 16) & 0xFFu;
  return float4(float3(channels) / 255.0f, coverage);
}
