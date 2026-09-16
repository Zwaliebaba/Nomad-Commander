// NeuronClient/Shaders/GlyphPS.hlsl
//
// The pixel half of the text pass. Compiled into CompiledShaders/GlyphPS.h as g_GlyphPS.
//
// Texture2D<uint> and Load, with no sampler anywhere — not because a sampler is disallowed (it has not been since
// 2026-09-16) but because Load takes integer texel coordinates and has no filtering to switch on. That is *why* a
// glyph authored as a bit pattern reaches the glass as that bit pattern (AGENTS.md §5).
//
// A zero texel is discarded rather than blended. discard does not write the pixel at all, which is what lets an
// opaque panel sit under opaque text with no alpha channel between them, and stays the cheaper path.
Texture2D<uint> g_glyphAtlas : register(t0);

float4 main(float4 _position : SV_Position, float2 _texel : TEXCOORD0, nointerpolation uint _colorRgba : COLOR0) : SV_Target
{
  // Truncation, not rounding: the interpolated coordinate runs from the texel's left edge to its right, so every
  // sample inside a texel floors to that texel and the scaling stays exactly integral.
  const int2 texel = int2(_texel);
  if (g_glyphAtlas.Load(int3(texel, 0)) == 0u)
  {
    discard;
  }
  const uint4 channels = uint4(_colorRgba, _colorRgba >> 8, _colorRgba >> 16, _colorRgba >> 24) & 0xFFu;
  return float4(channels) / 255.0f;
}
