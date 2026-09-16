// NeuronClient/Shaders/PrimitivePS.hlsl
//
// The pixel half of the 2D primitive pass: the packed colour unpacked and written, with no sampler and no blending --
// this pass needs neither. Compiled into CompiledShaders/PrimitivePS.h as g_PrimitivePS.
//
// The byte order is the scene target's own: red in the low byte, alpha in the high one, matching
// DXGI_FORMAT_R8G8B8A8_UNORM read back on a little-endian machine. So 0xFF0000FF is opaque red, and a colour authored
// 0xAA in a channel reaches the glass as 0xAA (AGENTS.md R12: _UNORM, not _SRGB).
float4 main(float4 _position : SV_Position, nointerpolation uint _colorRgba : COLOR0) : SV_Target
{
  const uint4 channels = uint4(_colorRgba, _colorRgba >> 8, _colorRgba >> 16, _colorRgba >> 24) & 0xFFu;
  return float4(channels) / 255.0f;
}
