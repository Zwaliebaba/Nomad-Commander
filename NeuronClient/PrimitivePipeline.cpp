// NeuronClient/PrimitivePipeline.cpp
#include "pch.h"
#include "PrimitivePipeline.h"
// Build output (AGENTS.md §2): one header per shader, declaring `const BYTE g_<Shader>[]`. This is the one translation
// unit that includes them. BYTE comes from <windows.h>, which pch.h brings in through NeuronCore.h.
#include "CompiledShaders/PrimitiveVS.h"
#include "CompiledShaders/PrimitivePS.h"

namespace Neuron
{

std::span<const std::byte> PrimitiveVertexShader() noexcept
{
  return std::as_bytes(std::span{g_PrimitiveVS});
}

std::span<const std::byte> PrimitivePixelShader() noexcept
{
  return std::as_bytes(std::span{g_PrimitivePS});
}

} // namespace Neuron
