// NeuronClient/SceneTarget.h
#pragma once

#include "GraphicsDevice.h"
#include "NeuronCore.h"

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <vector>

namespace Neuron
{

/// Why a render target could not be created, or why a frame could not be presented. Shared by SceneTarget and
/// SwapChainTarget, because the answers are the same set and a caller handles them the same way.
enum class TargetFault : std::uint8_t
{
  None,
  DeviceRemoved,
  BadFormat,
  OutOfVideoMemory,
  Allocation
};

/// Maps an HRESULT onto the fault a caller can act on. Anything unrecognized is Allocation, which is what almost every
/// remaining D3D12 creation failure is.
[[nodiscard]] TargetFault TargetFaultFromResult(HRESULT _result) noexcept;

/// The 1920x1080 colour surface the game draws into, and nothing else draws into (AGENTS.md R12, ADR-009). Every pass
/// renders here at exactly this size, whatever the window turned out to be; the present step is the only thing that
/// ever looks at the difference.
///
/// It is the game's own target rather than a test-only object, which is what lets the suite inspect the same pixels a
/// player would see (Plan/Roadmap.md A12).
class SceneTarget
{
public:
  struct Desc
  {
    std::uint32_t widthPixels;
    std::uint32_t heightPixels;
    DXGI_FORMAT colorFormat;
    /// The colour Clear uses, and the one the resource is created with.
    ///
    /// The two have to be the same value, so there is only one of them. A resource carries an optimized clear value
    /// and clearing it to anything else makes the driver take a slow path -- the debug layer says so, once per frame,
    /// which is how this was found: 3,380 warnings in a 22-second run of the game.
    float clearColor[4];
  };

  SceneTarget() = default;
  SceneTarget(const SceneTarget&) = delete;
  SceneTarget& operator=(const SceneTarget&) = delete;
  SceneTarget(SceneTarget&&) = delete;
  SceneTarget& operator=(SceneTarget&&) = delete;
  ~SceneTarget() = default;

  [[nodiscard]] static bool Create(GraphicsDevice& _device, const Desc& _desc, SceneTarget& _outTarget) noexcept;

  [[nodiscard]] ID3D12Resource* Resource() const noexcept
  {
    return m_resource.Get();
  }

  [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE RenderTargetView() const noexcept
  {
    return m_renderTargetView;
  }

  [[nodiscard]] std::uint32_t WidthPixels() const noexcept
  {
    return m_widthPixels;
  }

  [[nodiscard]] std::uint32_t HeightPixels() const noexcept
  {
    return m_heightPixels;
  }

  [[nodiscard]] DXGI_FORMAT ColorFormat() const noexcept
  {
    return m_colorFormat;
  }

  [[nodiscard]] TargetFault Fault() const noexcept
  {
    return m_fault;
  }

  [[nodiscard]] HRESULT Result() const noexcept
  {
    return m_result;
  }

  /// The state the resource is in as far as this object knows, which is how Transition knows what to transition from.
  [[nodiscard]] D3D12_RESOURCE_STATES State() const noexcept
  {
    return m_state;
  }

  /// Records a transition to the given state, and remembers it. A transition to the state it is already in records
  /// nothing: D3D12 rejects a barrier whose before and after states are equal.
  void Transition(ID3D12GraphicsCommandList* _commandList, D3D12_RESOURCE_STATES _state) noexcept;

  /// Records a clear of the whole target to the Desc's colour. The caller has already put it in RENDER_TARGET.
  ///
  /// It takes no colour on purpose: the only colour that can be cleared to without the driver taking a slow path is
  /// the one the resource was created with, so this cannot be handed a different one.
  void Clear(ID3D12GraphicsCommandList* _commandList) const noexcept;

  /// Copies the target back to system memory and waits for it, one pixel per element, row by row with no padding.
  ///
  /// Synchronous by design: it submits its own copy, waits on its own fence and maps the result. Nothing in a frame
  /// calls it; the suite does, to look at the pixels the player would see.
  ///
  /// Not noexcept, and deliberately: it sizes the caller's vector, which allocates two million pixels' worth. An
  /// allocation inside a noexcept function is std::terminate on a machine low on memory (the defect NC-020 round 5
  /// found in Window::Create), and this one is not on a frame path where the exception would have nowhere to go.
  [[nodiscard]] bool ReadBack(std::vector<std::uint32_t>& _outPixels);

private:
  Microsoft::WRL::ComPtr<ID3D12Device> m_device;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_resource;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_renderTargetHeap;
  D3D12_CPU_DESCRIPTOR_HANDLE m_renderTargetView{};
  D3D12_RESOURCE_STATES m_state = D3D12_RESOURCE_STATE_COMMON;
  float m_clearColor[4] = {};
  HRESULT m_result = S_OK;
  std::uint32_t m_widthPixels = 0;
  std::uint32_t m_heightPixels = 0;
  DXGI_FORMAT m_colorFormat = DXGI_FORMAT_UNKNOWN;
  TargetFault m_fault = TargetFault::None;
};

} // namespace Neuron
