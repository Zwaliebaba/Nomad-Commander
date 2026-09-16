// NeuronClient/Shaders/MeshVS.hlsl
//
// The vertex half of the map's mesh pass: a position through the view-projection, and a packed colour unpacked and
// passed on. Compiled into CompiledShaders/MeshVS.h as g_MeshVS (AGENTS.md §2, R13).
//
// The matrix arrives already transposed (Camera::ViewProjection does it), so there is no transpose here and no mul
// order to remember.
cbuffer MeshConstants : register(b0)
{
  float4x4 g_viewProjection;
  uint g_tintRgba;
};

struct Interpolants
{
  float4 position : SV_Position;
  float4 color : COLOR0;
};

float4 UnpackRgba(uint _packed)
{
  // Red in the low byte, as everywhere else in this client.
  const uint4 channels = uint4(_packed, _packed >> 8, _packed >> 16, _packed >> 24) & 0xFFu;
  return float4(channels) / 255.0f;
}

Interpolants main(float3 _positionUnits : POSITION, uint _colorRgba : COLOR)
{
  Interpolants output;
  // The position goes on the LEFT. DirectXMath builds row-vector matrices, Camera::ViewProjection transposes on the
  // way out, and HLSL packs a constant-buffer matrix column-major -- which together mean the matrix in this shader is
  // the one DirectXMath built, so `mul(position, matrix)` is the row-vector multiply it was built for. The other
  // arrangement (`mul(matrix, position)`) compiles, draws something plausible, and gets the depth wrong.
  output.position = mul(float4(_positionUnits, 1.0f), g_viewProjection);
  // The vertex colour times the per-draw tint. This is the whole of the shading model: there is no light, no normal
  // and no material (NC-027's out-of-scope list). A sphere reads as a sphere because MeshBuilder gives its vertices
  // the highlight, body and limb colours the UI spec already describes for the 2D map (UI §2).
  output.color = UnpackRgba(_colorRgba) * UnpackRgba(g_tintRgba);
  return output;
}
