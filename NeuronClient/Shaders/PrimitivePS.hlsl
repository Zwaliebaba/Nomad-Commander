// NeuronClient/Shaders/PrimitivePS.hlsl
//
// The pixel half of the 2D primitive pass. Phase 0 (NC-006) proves the build-time shader path; NC-022 makes it real:
// the packed color unpacked and written, with no sampler and no blending -- this pass needs neither. Compiled into
// CompiledShaders/PrimitivePS.h as g_PrimitivePS.
float4 main() : SV_Target
{
  return float4(1.0f, 1.0f, 1.0f, 1.0f);
}
