// NeuronClient/GlyphPipeline.h
#pragma once

#include "GraphicsDevice.h"
#include "NeuronCore.h"
#include "SceneTarget.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Neuron
{

/// The root signature, the one pipeline state and the descriptor heap the text pass draws through.
///
/// The heap is owned here rather than by the text renderer because it is the pass's, not the atlas's: a few slots, so
/// that a second thing wanting a texture in this pass has somewhere to put it without another heap being created.
class GlyphPipeline
{
public:
  /// The screen size, as the two root constants the vertex shader turns pixels into clip space with.
  static constexpr UINT ROOT_CONSTANT_COUNT = 2;

  /// Slots in the shader-visible heap. One is the glyph atlas; the rest are room to grow without a second heap.
  static constexpr UINT DESCRIPTOR_SLOTS = 8;

  /// Where the glyph atlas's shader resource view goes.
  static constexpr UINT ATLAS_SLOT = 0;

  GlyphPipeline() = default;
  GlyphPipeline(const GlyphPipeline&) = delete;
  GlyphPipeline& operator=(const GlyphPipeline&) = delete;
  GlyphPipeline(GlyphPipeline&&) = delete;
  GlyphPipeline& operator=(GlyphPipeline&&) = delete;
  ~GlyphPipeline() = default;

  [[nodiscard]] static bool Create(GraphicsDevice& _device, GlyphPipeline& _outPipeline) noexcept;

  [[nodiscard]] ID3D12RootSignature* RootSignature() const noexcept
  {
    return m_rootSignature.Get();
  }

  [[nodiscard]] ID3D12PipelineState* Pipeline() const noexcept
  {
    return m_pipeline.Get();
  }

  [[nodiscard]] ID3D12DescriptorHeap* DescriptorHeap() const noexcept
  {
    return m_descriptorHeap.Get();
  }

  /// Where a view for that slot is written, and where the shader finds it.
  [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle(UINT _slot) const noexcept;
  [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(UINT _slot) const noexcept;

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
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipeline;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_descriptorHeap;
  UINT m_descriptorSize = 0;
  HRESULT m_result = S_OK;
  TargetFault m_fault = TargetFault::None;
};

} // namespace Neuron
