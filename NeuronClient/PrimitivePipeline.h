// NeuronClient/PrimitivePipeline.h
#pragma once

#include "GraphicsDevice.h"
#include "NeuronCore.h"
#include "SceneTarget.h"

#include <cstddef>
#include <d3d12.h>
#include <span>
#include <wrl/client.h>

namespace Neuron
{

// The compiled vertex and pixel shaders of the 2D primitive pass, as the bytes the build produced from
// Shaders/Primitive{VS,PS}.hlsl (AGENTS.md R13: shaders are compiled at build time, never at runtime).

[[nodiscard]] std::span<const std::byte> PrimitiveVertexShader() noexcept;
[[nodiscard]] std::span<const std::byte> PrimitivePixelShader() noexcept;

/// The root signature and the two pipeline states every 2D primitive is drawn with: one for triangles and one for
/// lines, differing only in their primitive topology type.
///
/// Both are built from PipelineDefaults and override nothing. Opaque is what filled rectangles, polygons and circles
/// need; a one-pixel line is aliased on purpose.
class PrimitivePipeline
{
public:
  /// The screen size, as the two root constants the vertex shader turns pixels into clip space with.
  static constexpr UINT ROOT_CONSTANT_COUNT = 2;

  PrimitivePipeline() = default;
  PrimitivePipeline(const PrimitivePipeline&) = delete;
  PrimitivePipeline& operator=(const PrimitivePipeline&) = delete;
  PrimitivePipeline(PrimitivePipeline&&) = delete;
  PrimitivePipeline& operator=(PrimitivePipeline&&) = delete;
  ~PrimitivePipeline() = default;

  [[nodiscard]] static bool Create(GraphicsDevice& _device, PrimitivePipeline& _outPipeline) noexcept;

  [[nodiscard]] ID3D12RootSignature* RootSignature() const noexcept
  {
    return m_rootSignature.Get();
  }

  [[nodiscard]] ID3D12PipelineState* TrianglePipeline() const noexcept
  {
    return m_trianglePipeline.Get();
  }

  [[nodiscard]] ID3D12PipelineState* LinePipeline() const noexcept
  {
    return m_linePipeline.Get();
  }

  [[nodiscard]] TargetFault Fault() const noexcept
  {
    return m_fault;
  }

  [[nodiscard]] HRESULT Result() const noexcept
  {
    return m_result;
  }

private:
  Microsoft::WRL::ComPtr<ID3D12RootSignature> m_rootSignature;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_trianglePipeline;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_linePipeline;
  HRESULT m_result = S_OK;
  TargetFault m_fault = TargetFault::None;
};

} // namespace Neuron
