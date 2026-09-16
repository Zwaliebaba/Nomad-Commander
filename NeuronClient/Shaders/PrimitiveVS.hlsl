// NeuronClient/Shaders/PrimitiveVS.hlsl
//
// The vertex half of the 2D primitive pass. Phase 0 (NC-006) proves the build-time shader path with the smallest
// shader that compiles; NC-022 makes it real: pixel positions to clip space through root constants, a packed color
// passed through. Compiled by the .vcxproj's FXCompile step into CompiledShaders/PrimitiveVS.h as g_PrimitiveVS
// (AGENTS.md §2, R13); never at runtime.
float4 main(float2 _position : POSITION) : SV_Position
{
  return float4(_position, 0.0f, 1.0f);
}
