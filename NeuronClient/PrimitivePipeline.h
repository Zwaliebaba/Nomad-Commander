// NeuronClient/PrimitivePipeline.h
#pragma once

#include <cstddef>
#include <span>

namespace Neuron
{

// The compiled vertex and pixel shaders of the 2D primitive pass, as the bytes the build produced from
// Shaders/Primitive{VS,PS}.hlsl (AGENTS.md R13: shaders are compiled at build time, never at runtime). NC-022 grows
// this file into the pipeline builder; until then it exposes the blobs and nothing else.

[[nodiscard]] std::span<const std::byte> PrimitiveVertexShader() noexcept;
[[nodiscard]] std::span<const std::byte> PrimitivePixelShader() noexcept;

} // namespace Neuron
