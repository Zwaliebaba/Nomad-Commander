// NeuronClient/DepthTarget.h
#pragma once

#include "GraphicsDevice.h"
#include "NeuronCore.h"
#include "SceneTarget.h"

#include <cstdint>
#include <d3d12.h>
#include <wrl/client.h>

namespace Neuron
{

/// The depth buffer the 3D map is tested against: `D32_FLOAT`, the size of the screen.
///
/// **It binds beside the SCENE target, not the back buffer.** The task that planned this said back buffer, and
/// ADR-009 changed that before it was built: nothing draws into a back buffer any more, so a depth buffer sized to
/// the window would be the wrong size for everything that draws.
///
/// A 2D pass binds no depth at all — `PrimitiveBatch` and `TextRenderer` set one render target and a null DSV, which
/// is why the desk is unaffected by any of this (GDD §13: perspective is for the map alone).
class DepthTarget
{
public:
  /// `D32_FLOAT` rather than `D24_UNORM_S8_UINT`: nothing here stencils, and a 32-bit float depth is both simpler and
  /// better behaved far from the camera.
  static constexpr DXGI_FORMAT FORMAT = DXGI_FORMAT_D32_FLOAT;

  /// Cleared to 1, the far plane, because the map is drawn with a standard less-than test.
  static constexpr float CLEAR_DEPTH = 1.0f;

  DepthTarget() = default;
  DepthTarget(const DepthTarget&) = delete;
  DepthTarget& operator=(const DepthTarget&) = delete;
  DepthTarget(DepthTarget&&) = delete;
  DepthTarget& operator=(DepthTarget&&) = delete;
  ~DepthTarget() = default;

  [[nodiscard]] static bool Create(GraphicsDevice& _device, std::uint32_t _widthPixels, std::uint32_t _heightPixels,
                                   DepthTarget& _outTarget) noexcept;

  [[nodiscard]] ID3D12Resource* Resource() const noexcept
  {
    return m_resource.Get();
  }

  [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const noexcept
  {
    return m_depthStencilView;
  }

  [[nodiscard]] std::uint32_t WidthPixels() const noexcept
  {
    return m_widthPixels;
  }

  [[nodiscard]] std::uint32_t HeightPixels() const noexcept
  {
    return m_heightPixels;
  }

  /// Records a clear to the far plane. The caller has it in DEPTH_WRITE.
  void Clear(ID3D12GraphicsCommandList* _commandList) const noexcept;

  [[nodiscard]] TargetFault Fault() const noexcept
  {
    return m_fault;
  }

  [[nodiscard]] HRESULT Result() const noexcept
  {
    return m_result;
  }

private:
  Microsoft::WRL::ComPtr<ID3D12Device> m_device;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_resource;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_depthStencilHeap;
  D3D12_CPU_DESCRIPTOR_HANDLE m_depthStencilView{};
  HRESULT m_result = S_OK;
  std::uint32_t m_widthPixels = 0;
  std::uint32_t m_heightPixels = 0;
  TargetFault m_fault = TargetFault::None;
};

} // namespace Neuron
