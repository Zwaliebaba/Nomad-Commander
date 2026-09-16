// NeuronClient/Shaders/MeshPS.hlsl
//
// The pixel half of the map's mesh pass. Compiled into CompiledShaders/MeshPS.h as g_MeshPS.
//
// It writes the interpolated vertex colour and nothing else: no light, no normal, no texture, no fog. NC-027's scope
// permits a lighting model and deliberately does not add one.
float4 main(float4 _position : SV_Position, float4 _color : COLOR0) : SV_Target
{
  return _color;
}
