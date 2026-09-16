// NeuronClient/SceneTarget.cpp
#include "pch.h"
#include "SceneTarget.h"
#include "Debug.h"

#include <cstring>

namespace Neuron
{

namespace
{

using Microsoft::WRL::ComPtr;

/// A texture row in a copy to or from a buffer is aligned to this, which is why a readback cannot simply memcpy the
/// whole mapped range: the source rows are padded and the destination's are not.
constexpr std::uint32_t COPY_ROW_ALIGNMENT = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

/// A fully specified heap description.
///
/// Every field is named rather than zero-initialized and patched, because several D3D12 enums have no zero-valued
/// enumerator -- D3D12_HEAP_TYPE starts at 1 -- so `D3D12_HEAP_PROPERTIES heap{}` puts a value in the struct that the
/// enum has no name for. It is overwritten a line later and the API never sees it, but clang-tidy is right that the
/// value existed, and R14 already says these descriptions are written by hand here.
[[nodiscard]] D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE _type) noexcept
{
  return D3D12_HEAP_PROPERTIES{.Type = _type,
                               .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                               .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
                               .CreationNodeMask = 1,
                               .VisibleNodeMask = 1};
}

/// Blocks until the queue has passed the value, on a fence and event this function owns.
[[nodiscard]] bool WaitForQueue(ID3D12Device* _device, ID3D12CommandQueue* _queue) noexcept
{
  ComPtr<ID3D12Fence> fence;
  if (FAILED(_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
  {
    return false;
  }
  const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
  if (event == nullptr)
  {
    return false;
  }
  bool waited = false;
  if (SUCCEEDED(_queue->Signal(fence.Get(), 1)) && SUCCEEDED(fence->SetEventOnCompletion(1, event)))
  {
    waited = WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0;
  }
  CloseHandle(event);
  return waited;
}

} // namespace

TargetFault TargetFaultFromResult(HRESULT _result) noexcept
{
  switch (_result)
  {
  case DXGI_ERROR_DEVICE_REMOVED:
  case DXGI_ERROR_DEVICE_RESET:
  case DXGI_ERROR_DEVICE_HUNG:
    return TargetFault::DeviceRemoved;
  case E_OUTOFMEMORY:
    return TargetFault::OutOfVideoMemory;
  case E_INVALIDARG:
    return TargetFault::BadFormat;
  default:
    return SUCCEEDED(_result) ? TargetFault::None : TargetFault::Allocation;
  }
}

bool SceneTarget::Create(GraphicsDevice& _device, const Desc& _desc, SceneTarget& _outTarget) noexcept
{
  NOMAD_ASSERT(_outTarget.m_resource == nullptr);
  NOMAD_ASSERT(_desc.widthPixels > 0 && _desc.heightPixels > 0);
  _outTarget.m_fault = TargetFault::None;
  _outTarget.m_result = S_OK;
  _outTarget.m_device = _device.Device();
  _outTarget.m_queue = _device.Queue();
  _outTarget.m_widthPixels = _desc.widthPixels;
  _outTarget.m_heightPixels = _desc.heightPixels;
  _outTarget.m_colorFormat = _desc.colorFormat;

  // R14: no d3dx12.h. The heap properties and the resource description are written out.
  const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);

  D3D12_RESOURCE_DESC texture{};
  texture.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texture.Alignment = 0;
  texture.Width = _desc.widthPixels;
  texture.Height = _desc.heightPixels;
  texture.DepthOrArraySize = 1;
  texture.MipLevels = 1;
  texture.Format = _desc.colorFormat;
  // One sample. ADR-009 makes a multisampled scene target possible for the first time -- it is not a back buffer -- but
  // whether to pay for it is NC-027's to decide with a measurement, and this frame loop resolves nothing.
  texture.SampleDesc.Count = 1;
  texture.SampleDesc.Quality = 0;
  texture.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  texture.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  // The clear value the resource is created with is the one Clear uses, because Desc carries one colour and both read
  // it. Creating it with one value and clearing to another makes the driver take a slow path, and the debug layer
  // reports it once per frame -- which is exactly how the first version of this file was caught.
  D3D12_CLEAR_VALUE clearValue{};
  clearValue.Format = _desc.colorFormat;
  for (std::size_t channel = 0; channel < 4; ++channel)
  {
    clearValue.Color[channel] = _desc.clearColor[channel];
    _outTarget.m_clearColor[channel] = _desc.clearColor[channel];
  }

  _outTarget.m_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
  _outTarget.m_result = _outTarget.m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &texture, _outTarget.m_state, &clearValue,
                                                                     IID_PPV_ARGS(&_outTarget.m_resource));
  if (FAILED(_outTarget.m_result))
  {
    _outTarget.m_fault = TargetFaultFromResult(_outTarget.m_result);
    return false;
  }

  D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
  heapDesc.NumDescriptors = 1;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  heapDesc.NodeMask = 0;
  _outTarget.m_result = _outTarget.m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_outTarget.m_renderTargetHeap));
  if (FAILED(_outTarget.m_result))
  {
    _outTarget.m_fault = TargetFaultFromResult(_outTarget.m_result);
    return false;
  }

  _outTarget.m_renderTargetView = _outTarget.m_renderTargetHeap->GetCPUDescriptorHandleForHeapStart();
  _outTarget.m_device->CreateRenderTargetView(_outTarget.m_resource.Get(), nullptr, _outTarget.m_renderTargetView);
  return true;
}

void SceneTarget::Transition(ID3D12GraphicsCommandList* _commandList, D3D12_RESOURCE_STATES _state) noexcept
{
  if (_commandList == nullptr || m_resource == nullptr || m_state == _state)
  {
    return;
  }
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = m_resource.Get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = m_state;
  barrier.Transition.StateAfter = _state;
  _commandList->ResourceBarrier(1, &barrier);
  m_state = _state;
}

void SceneTarget::Clear(ID3D12GraphicsCommandList* _commandList) const noexcept
{
  NOMAD_ASSERT(m_state == D3D12_RESOURCE_STATE_RENDER_TARGET);
  _commandList->ClearRenderTargetView(m_renderTargetView, m_clearColor, 0, nullptr);
}

bool SceneTarget::ReadBack(std::vector<std::uint32_t>& _outPixels)
{
  if (m_resource == nullptr || m_device == nullptr || m_queue == nullptr)
  {
    return false;
  }

  D3D12_RESOURCE_DESC texture = m_resource->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT64 totalBytes = 0;
  m_device->GetCopyableFootprints(&texture, 0, 1, 0, &footprint, nullptr, nullptr, &totalBytes);
  NOMAD_ASSERT(footprint.Footprint.RowPitch % COPY_ROW_ALIGNMENT == 0);

  const D3D12_HEAP_PROPERTIES heap = HeapProperties(D3D12_HEAP_TYPE_READBACK);

  D3D12_RESOURCE_DESC buffer{};
  buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  buffer.Alignment = 0;
  buffer.Width = totalBytes;
  buffer.Height = 1;
  buffer.DepthOrArraySize = 1;
  buffer.MipLevels = 1;
  buffer.Format = DXGI_FORMAT_UNKNOWN;
  buffer.SampleDesc.Count = 1;
  buffer.SampleDesc.Quality = 0;
  buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  buffer.Flags = D3D12_RESOURCE_FLAG_NONE;

  ComPtr<ID3D12Resource> readback;
  if (FAILED(m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                               IID_PPV_ARGS(&readback))))
  {
    return false;
  }

  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> commandList;
  if (FAILED(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
      FAILED(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList))))
  {
    return false;
  }

  const D3D12_RESOURCE_STATES stateBefore = m_state;
  Transition(commandList.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);

  D3D12_TEXTURE_COPY_LOCATION destination{};
  destination.pResource = readback.Get();
  destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  destination.PlacedFootprint = footprint;
  D3D12_TEXTURE_COPY_LOCATION source{};
  source.pResource = m_resource.Get();
  source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  source.SubresourceIndex = 0;
  commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

  Transition(commandList.Get(), stateBefore);
  if (FAILED(commandList->Close()))
  {
    return false;
  }
  ID3D12CommandList* const lists[] = {commandList.Get()};
  m_queue->ExecuteCommandLists(1, lists);
  if (!WaitForQueue(m_device.Get(), m_queue.Get()))
  {
    return false;
  }

  void* mapped = nullptr;
  const D3D12_RANGE readEverything{0, static_cast<SIZE_T>(totalBytes)};
  if (FAILED(readback->Map(0, &readEverything, &mapped)))
  {
    return false;
  }
  _outPixels.assign(static_cast<std::size_t>(m_widthPixels) * m_heightPixels, 0u);
  const std::byte* const rows = static_cast<const std::byte*>(mapped);
  for (std::uint32_t y = 0; y < m_heightPixels; ++y)
  {
    const std::byte* const row = rows + static_cast<std::size_t>(y) * footprint.Footprint.RowPitch;
    std::memcpy(_outPixels.data() + static_cast<std::size_t>(y) * m_widthPixels, row,
                static_cast<std::size_t>(m_widthPixels) * sizeof(std::uint32_t));
  }
  const D3D12_RANGE wroteNothing{0, 0};
  readback->Unmap(0, &wroteNothing);
  return true;
}

} // namespace Neuron
