// NeuronClient/Shaders/PresentPS.hlsl
//
// The pixel half of the present pass: the scene target read into the back buffer. Compiled into
// CompiledShaders/PresentPS.h as g_PresentPS (AGENTS.md §2, R13).
//
// Two static samplers rather than one, chosen by a root constant, because ADR-009 asks for point sampling at an exact
// integer multiple and bilinear otherwise, and a static sampler is baked into the root signature where a branch is
// free. The third case ADR-009 names -- exactly 1:1 -- never reaches this shader at all: the pass copies instead, so
// "no filtering" means no sampling rather than a sampler that happens to land on texel centres.
Texture2D<float4> g_scene : register(t0);
SamplerState g_pointSampler : register(s0);
SamplerState g_linearSampler : register(s1);

cbuffer PresentConstants : register(b0)
{
  uint g_useLinearFilter;
};

float4 main(float4 _position : SV_Position, float2 _texcoord : TEXCOORD0) : SV_Target
{
  if (g_useLinearFilter != 0)
  {
    return g_scene.Sample(g_linearSampler, _texcoord);
  }
  return g_scene.Sample(g_pointSampler, _texcoord);
}
