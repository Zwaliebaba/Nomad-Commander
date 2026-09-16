// NeuronClient/MeshPipeline.h
#pragma once

#include "Camera.h"
#include "DepthTarget.h"
#include "GraphicsDevice.h"
#include "NeuronCore.h"
#include "SceneTarget.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Neuron
{

/// The one pipeline the map's geometry is drawn with: indexed triangles, depth on, a view-projection and a per-draw
/// tint in root constants.
///
/// **One pipeline, not a family** (NC-027's note). A second is a proposal in a report with what it buys.
class MeshPipeline
{
public:
  /// Sixteen floats of view-projection and one packed tint.
  static constexpr UINT ROOT_CONSTANT_COUNT = 17;

  MeshPipeline() = default;
  MeshPipeline(const MeshPipeline&) = delete;
  MeshPipeline& operator=(const MeshPipeline&) = delete;
  MeshPipeline(MeshPipeline&&) = delete;
  MeshPipeline& operator=(MeshPipeline&&) = delete;
  ~MeshPipeline() = default;

  [[nodiscard]] static bool Create(GraphicsDevice& _device, MeshPipeline& _outPipeline) noexcept;

  /// Binds the pipeline and sets the frame's camera. The caller has already bound the render target and the depth.
  void Begin(ID3D12GraphicsCommandList* _commandList, const Camera& _camera) noexcept;

  /// The colour the next draw is multiplied by. White leaves the vertex colours alone.
  void SetTint(ID3D12GraphicsCommandList* _commandList, std::uint32_t _tintRgba) noexcept;

  [[nodiscard]] ID3D12RootSignature* RootSignature() const noexcept
  {
    return m_rootSignature.Get();
  }

  [[nodiscard]] ID3D12PipelineState* Pipeline() const noexcept
  {
    return m_pipeline.Get();
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
  Microsoft::WRL::ComPtr<ID3D12PipelineState> m_pipeline;
  HRESULT m_result = S_OK;
  TargetFault m_fault = TargetFault::None;
};

} // namespace Neuron
