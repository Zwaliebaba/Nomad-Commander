// NeuronClient/SwapChainTarget.cpp
#include "pch.h"
#include "SwapChainTarget.h"
#include "Debug.h"

namespace Neuron
{

namespace
{

using Microsoft::WRL::ComPtr;

/// Records a transition of one back buffer. Written out rather than taken from d3dx12.h, which R14 excludes.
void TransitionBackBuffer(ID3D12GraphicsCommandList* _commandList, ID3D12Resource* _resource, D3D12_RESOURCE_STATES _before,
                          D3D12_RESOURCE_STATES _after) noexcept
{
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = _resource;
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = _before;
  barrier.Transition.StateAfter = _after;
  _commandList->ResourceBarrier(1, &barrier);
}

} // namespace

SwapChainTarget::~SwapChainTarget()
{
  // The GPU may still be reading a back buffer this object owns. Releasing one underneath it is the defect the whole
  // fence exists to prevent, so the wait happens here too and not only where a caller remembers it.
  WaitForGpu();
  if (m_fenceEvent != nullptr)
  {
    CloseHandle(m_fenceEvent);
    m_fenceEvent = nullptr;
  }
}

bool SwapChainTarget::Create(GraphicsDevice& _device, HWND _window, const Desc& _desc, SwapChainTarget& _outTarget) noexcept
{
  NOMAD_ASSERT(_outTarget.m_swapChain == nullptr);
  NOMAD_ASSERT(_desc.widthPixels > 0 && _desc.heightPixels > 0);
  _outTarget.m_fault = TargetFault::None;
  _outTarget.m_result = S_OK;
  _outTarget.m_device = _device.Device();
  _outTarget.m_queue = _device.Queue();
  _outTarget.m_widthPixels = _desc.widthPixels;
  _outTarget.m_heightPixels = _desc.heightPixels;

  DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
  swapChainDesc.Width = _desc.widthPixels;
  swapChainDesc.Height = _desc.heightPixels;
  swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.Stereo = FALSE;
  // One sample, and not a choice: DXGI does not multisample a flip-model back buffer (AGENTS.md §5).
  swapChainDesc.SampleDesc.Count = 1;
  swapChainDesc.SampleDesc.Quality = 0;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.BufferCount = BUFFER_COUNT;
  // NONE, not STRETCH: the back buffer is exactly the client area, so there is nothing for DXGI to scale, and the one
  // scale in this frame is the present pass's, which knows about letterboxing and filtering (ADR-009).
  swapChainDesc.Scaling = DXGI_SCALING_NONE;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  swapChainDesc.Flags = 0;

  ComPtr<IDXGISwapChain1> swapChain;
  _outTarget.m_result =
    _device.Factory()->CreateSwapChainForHwnd(_outTarget.m_queue.Get(), _window, &swapChainDesc, nullptr, nullptr, &swapChain);
  if (FAILED(_outTarget.m_result))
  {
    _outTarget.m_fault = TargetFaultFromResult(_outTarget.m_result);
    return false;
  }
  _outTarget.m_result = swapChain.As(&_outTarget.m_swapChain);
  if (FAILED(_outTarget.m_result))
  {
    _outTarget.m_fault = TargetFaultFromResult(_outTarget.m_result);
    return false;
  }

  // DXGI's own Alt+Enter would take the window fullscreen behind the game's back. ADR-010 already gives the whole
  // monitor; an exclusive-fullscreen transition is out of scope and would break the present scale.
  NOMAD_VERIFY(SUCCEEDED(_device.Factory()->MakeWindowAssociation(_window, DXGI_MWA_NO_ALT_ENTER)));

  D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  heapDesc.NumDescriptors = BUFFER_COUNT;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  heapDesc.NodeMask = 0;
  _outTarget.m_result = _outTarget.m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_outTarget.m_renderTargetHeap));
  if (FAILED(_outTarget.m_result))
  {
    _outTarget.m_fault = TargetFaultFromResult(_outTarget.m_result);
    return false;
  }

  const UINT descriptorSize = _outTarget.m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  D3D12_CPU_DESCRIPTOR_HANDLE view = _outTarget.m_renderTargetHeap->GetCPUDescriptorHandleForHeapStart();
  for (std::uint32_t index = 0; index < BUFFER_COUNT; ++index)
  {
    _outTarget.m_result = _outTarget.m_swapChain->GetBuffer(index, IID_PPV_ARGS(&_outTarget.m_backBuffers[index]));
    if (FAILED(_outTarget.m_result))
    {
      _outTarget.m_fault = TargetFaultFromResult(_outTarget.m_result);
      return false;
    }
    _outTarget.m_device->CreateRenderTargetView(_outTarget.m_backBuffers[index].Get(), nullptr, view);
    _outTarget.m_renderTargetViews[index] = view;
    view.ptr += descriptorSize;

    _outTarget.m_result =
      _outTarget.m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_outTarget.m_allocators[index]));
    if (FAILED(_outTarget.m_result))
    {
      _outTarget.m_fault = TargetFaultFromResult(_outTarget.m_result);
      return false;
    }
  }

  _outTarget.m_result = _outTarget.m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _outTarget.m_allocators[0].Get(), nullptr,
                                                               IID_PPV_ARGS(&_outTarget.m_commandList));
  if (FAILED(_outTarget.m_result))
  {
    _outTarget.m_fault = TargetFaultFromResult(_outTarget.m_result);
    return false;
  }
  // A command list is created open. BeginFrame resets it, so it starts closed like every later frame's does.
  _outTarget.m_result = _outTarget.m_commandList->Close();
  if (FAILED(_outTarget.m_result))
  {
    _outTarget.m_fault = TargetFaultFromResult(_outTarget.m_result);
    return false;
  }

  _outTarget.m_result = _outTarget.m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_outTarget.m_fence));
  if (FAILED(_outTarget.m_result))
  {
    _outTarget.m_fault = TargetFaultFromResult(_outTarget.m_result);
    return false;
  }
  _outTarget.m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (_outTarget.m_fenceEvent == nullptr)
  {
    _outTarget.m_result = HRESULT_FROM_WIN32(GetLastError());
    _outTarget.m_fault = TargetFault::Allocation;
    return false;
  }

  _outTarget.m_bufferIndex = _outTarget.m_swapChain->GetCurrentBackBufferIndex();
  return true;
}

ID3D12Resource* SwapChainTarget::BackBuffer() const noexcept
{
  return m_backBuffers[m_bufferIndex].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE SwapChainTarget::BackBufferView() const noexcept
{
  return m_renderTargetViews[m_bufferIndex];
}

ID3D12GraphicsCommandList* SwapChainTarget::BeginFrame(const float _clearColor[4]) noexcept
{
  if (m_swapChain == nullptr || m_frameOpen)
  {
    return nullptr;
  }
  m_bufferIndex = m_swapChain->GetCurrentBackBufferIndex();

  // The heart of it: this buffer was last drawn BUFFER_COUNT frames ago and the GPU may still be reading it. Nothing
  // touches it until the fence says that frame is done, which is what stops a frame overwriting a buffer in flight.
  const std::uint64_t waitFor = m_fenceValues[m_bufferIndex];
  if (waitFor != 0 && m_fence->GetCompletedValue() < waitFor)
  {
    if (FAILED(m_fence->SetEventOnCompletion(waitFor, m_fenceEvent)))
    {
      m_fault = TargetFault::DeviceRemoved;
      return nullptr;
    }
    WaitForSingleObject(m_fenceEvent, INFINITE);
  }

  if (FAILED(m_allocators[m_bufferIndex]->Reset()) || FAILED(m_commandList->Reset(m_allocators[m_bufferIndex].Get(), nullptr)))
  {
    m_fault = TargetFault::DeviceRemoved;
    return nullptr;
  }

  TransitionBackBuffer(m_commandList.Get(), m_backBuffers[m_bufferIndex].Get(), D3D12_RESOURCE_STATE_PRESENT,
                       D3D12_RESOURCE_STATE_RENDER_TARGET);
  const D3D12_CPU_DESCRIPTOR_HANDLE view = m_renderTargetViews[m_bufferIndex];
  m_commandList->ClearRenderTargetView(view, _clearColor, 0, nullptr);
  m_commandList->OMSetRenderTargets(1, &view, FALSE, nullptr);
  m_frameOpen = true;
  return m_commandList.Get();
}

bool SwapChainTarget::EndFrame() noexcept
{
  if (!m_frameOpen)
  {
    return false;
  }
  m_frameOpen = false;

  TransitionBackBuffer(m_commandList.Get(), m_backBuffers[m_bufferIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
                       D3D12_RESOURCE_STATE_PRESENT);
  m_result = m_commandList->Close();
  if (FAILED(m_result))
  {
    m_fault = TargetFaultFromResult(m_result);
    return false;
  }
  ID3D12CommandList* const lists[] = {m_commandList.Get()};
  m_queue->ExecuteCommandLists(1, lists);

  // Vsync, always. The game draws a desk: there is nothing here that a torn frame would buy.
  m_result = m_swapChain->Present(1, 0);
  if (FAILED(m_result))
  {
    m_fault = TargetFaultFromResult(m_result);
    return false;
  }

  ++m_lastSignalled;
  m_result = m_queue->Signal(m_fence.Get(), m_lastSignalled);
  if (FAILED(m_result))
  {
    m_fault = TargetFaultFromResult(m_result);
    return false;
  }
  m_fenceValues[m_bufferIndex] = m_lastSignalled;
  ++m_presentedFrames;
  return true;
}

void SwapChainTarget::WaitForGpu() noexcept
{
  if (m_queue == nullptr || m_fence == nullptr || m_fenceEvent == nullptr)
  {
    return;
  }
  ++m_lastSignalled;
  if (FAILED(m_queue->Signal(m_fence.Get(), m_lastSignalled)))
  {
    return;
  }
  if (m_fence->GetCompletedValue() < m_lastSignalled)
  {
    if (SUCCEEDED(m_fence->SetEventOnCompletion(m_lastSignalled, m_fenceEvent)))
    {
      WaitForSingleObject(m_fenceEvent, INFINITE);
    }
  }
  for (std::uint64_t& value : m_fenceValues)
  {
    value = m_lastSignalled;
  }
}

} // namespace Neuron
