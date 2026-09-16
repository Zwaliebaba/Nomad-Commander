// NeuronClient/Shaders/GlyphVS.hlsl
//
// The vertex half of the text pass: one quad a character, in pixels, carrying the texel it starts at. Compiled into
// CompiledShaders/GlyphVS.h as g_GlyphVS (AGENTS.md §2, R13).
//
// The texel coordinate INTERPOLATES, unlike the colour. A glyph is drawn one texel a pixel (ADR-016), so a pixel's
// centre lands at texel + 1/2 and truncates to that texel in the pixel shader. No sampler is involved anywhere, so
// there is nothing to filter and nothing to blur.
cbuffer GlyphConstants : register(b0)
{
  uint2 g_screenSizePixels;
};

struct Interpolants
{
  float4 position : SV_Position;
  float2 texel : TEXCOORD0;
  nointerpolation uint color : COLOR0;
};

Interpolants main(float2 _positionPixels : POSITION, float2 _texel : TEXCOORD, uint _colorRgba : COLOR)
{
  Interpolants output;
  const float2 normalized = _positionPixels / float2(g_screenSizePixels);
  output.position = float4(normalized * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
  output.texel = _texel;
  output.color = _colorRgba;
  return output;
}
