// NeuronClient/Shaders/PrimitiveVS.hlsl
//
// The vertex half of the 2D primitive pass: pixel positions to clip space, and a packed colour passed through
// untouched. Compiled by the .vcxproj's FXCompile step into CompiledShaders/PrimitiveVS.h as g_PrimitiveVS
// (AGENTS.md §2, R13); never at runtime.
//
// The screen size arrives as root constants rather than a constant buffer, because it is two integers that never
// change within a frame and a root constant costs no descriptor and no upload.
cbuffer PrimitiveConstants : register(b0)
{
  uint2 g_screenSizePixels;
};

struct Interpolants
{
  float4 position : SV_Position;
  // nointerpolation: this is a bit pattern, not a colour yet. Interpolating it across a triangle would blend the
  // packed bytes into each other and unpack to nonsense -- the pixel shader is what turns it into a colour.
  nointerpolation uint color : COLOR0;
};

Interpolants main(float2 _positionPixels : POSITION, uint _colorRgba : COLOR)
{
  Interpolants output;
  // A pixel coordinate names the pixel's top-left CORNER, so a rectangle from (20,30) to (30,40) covers pixels 20..29
  // and 30..39 exactly, with no half-pixel argument at the edges. y is flipped because pixel space runs down the
  // screen and clip space runs up it.
  const float2 normalized = _positionPixels / float2(g_screenSizePixels);
  output.position = float4(normalized * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
  output.color = _colorRgba;
  return output;
}
