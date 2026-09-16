// NeuronClient/Shaders/PresentVS.hlsl
//
// The vertex half of the present pass: the one pass that looks at the difference between the 1920x1080 scene target
// and the window's client area (ADR-009). Compiled by the .vcxproj's FXCompile step into
// CompiledShaders/PresentVS.h as g_PresentVS (AGENTS.md §2, R13); never at runtime.
//
// One triangle, no vertex buffer and no input layout: three vertices big enough to cover the viewport, generated from
// the vertex index. A triangle rather than two for a quad, so no pixel is shaded twice along a diagonal seam and no
// buffer has to exist to describe four corners.
struct Interpolants
{
  float4 position : SV_Position;
  float2 texcoord : TEXCOORD0;
};

Interpolants main(uint _vertexId : SV_VertexID)
{
  Interpolants output;
  // Vertex 0 -> (0,0), 1 -> (2,0), 2 -> (0,2). The texture coordinate runs past 1 on purpose: the part of the triangle
  // outside the viewport is clipped away, and what is left is exactly the unit square.
  output.texcoord = float2(float((_vertexId << 1) & 2), float(_vertexId & 2));
  // Texture space is y-down and clip space is y-up, which is the whole of the -2 and the +1.
  output.position = float4(output.texcoord * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
  return output;
}
