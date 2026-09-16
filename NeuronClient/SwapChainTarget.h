// NeuronClient/SwapChainTarget.h
#pragma once

#include "GraphicsDevice.h"
#include "NeuronCore.h"
#include "SceneTarget.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <cstdint>

namespace Neuron
{

/// The flip-model swap chain on the window, its back buffers, and the frame's command recording and fencing.
///
/// Two buffers, `DXGI_SWAP_EFFECT_FLIP_DISCARD`, `DXGI_FORMAT_R8G8B8A8_UNORM` -- not `_SRGB`, so a channel authored
/// 0xAA reaches the glass as 0xAA (AGENTS.md R12). It is never multisampled: DXGI will not multisample a flip-model
/// back buffer, which is a fact about the swap chain rather than a preference, and is why ADR-009 put the game's own
/// drawing in a scene target instead.
///
/// The back buffer is the size of the window's client area, which since ADR-010 is the monitor's. Nothing resizes: the
/// window is borderless, fixed and unresizable, so there is no WM_SIZE path here.
class SwapChainTarget
{
public:
  struct Desc
  {
    std::uint32_t widthPixels;
    std::uint32_t heightPixels;
  };

  /// Two, and stated once. A flip-model swap chain needs at least two, and a third buys latency this game has no use
  /// for -- it presents with vsync and draws a desk.
  static constexpr std::uint32_t BUFFER_COUNT = 2;

  SwapChainTarget() = default;
  SwapChainTarget(const SwapChainTarget&) = delete;
  SwapChainTarget& operator=(const SwapChainTarget&) = delete;
  SwapChainTarget(SwapChainTarget&&) = delete;
  SwapChainTarget& operator=(SwapChainTarget&&) = delete;
  ~SwapChainTarget();

  [[nodiscard]] static bool Create(GraphicsDevice& _device, HWND _window, const Desc& _desc, SwapChainTarget& _outTarget) noexcept;

  /// Waits until the buffer about to be drawn is free, resets its allocator, opens the command list, transitions the
  /// back buffer to a render target, clears it and binds it. Returns the open list, or nullptr on a fault.
  ///
  /// The clear colour is what shows in the letterbox bars when the client area is not the screen's shape, so it is the
  /// caller's to choose rather than a constant here.
  [[nodiscard]] ID3D12GraphicsCommandList* BeginFrame(const float _clearColor[4]) noexcept;

  /// Transitions the back buffer to present, closes and submits the list, presents with vsync, and signals the fence
  /// for the buffer just used. False on a fault, which Fault() then names.
  [[nodiscard]] bool EndFrame() noexcept;

  /// Blocks until the GPU has finished everything submitted so far. Called before the object is destroyed, and by a
  /// caller that is about to tear something down.
  void WaitForGpu() noexcept;

  [[nodiscard]] TargetFault Fault() const noexcept
  {
    return m_fault;
  }

  [[nodiscard]] HRESULT Result() const noexcept
  {
    return m_result;
  }

  [[nodiscard]] std::uint32_t WidthPixels() const noexcept
  {
    return m_widthPixels;
  }

  [[nodiscard]] std::uint32_t HeightPixels() const noexcept
  {
    return m_heightPixels;
  }

  /// The buffer the open frame is drawing into, and its view. Only meaningful between BeginFrame and EndFrame.
  [[nodiscard]] ID3D12Resource* BackBuffer() const noexcept;
  [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE BackBufferView() const noexcept;

  /// How many frames have been presented. The frame loop's own count, for the report and the log.
  [[nodiscard]] std::uint64_t PresentedFrames() const noexcept
  {
    return m_presentedFrames;
  }

private:
  Microsoft::WRL::ComPtr<ID3D12Device> m_device;
  Microsoft::WRL::ComPtr<ID3D12CommandQueue> m_queue;
  Microsoft::WRL::ComPtr<IDXGISwapChain4> m_swapChain;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> m_renderTargetHeap;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_backBuffers[BUFFER_COUNT];
  Microsoft::WRL::ComPtr<ID3D12CommandAllocator> m_allocators[BUFFER_COUNT];
  Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> m_commandList;
  Microsoft::WRL::ComPtr<ID3D12Fence> m_fence;
  HANDLE m_fenceEvent = nullptr;
  std::uint64_t m_fenceValues[BUFFER_COUNT] = {};
  std::uint64_t m_lastSignalled = 0;
  std::uint64_t m_presentedFrames = 0;
  D3D12_CPU_DESCRIPTOR_HANDLE m_renderTargetViews[BUFFER_COUNT] = {};
  UINT m_bufferIndex = 0;
  HRESULT m_result = S_OK;
  std::uint32_t m_widthPixels = 0;
  std::uint32_t m_heightPixels = 0;
  TargetFault m_fault = TargetFault::None;
  bool m_frameOpen = false;
};

} // namespace Neuron
