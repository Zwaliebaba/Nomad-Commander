// NeuronClient/DepthTarget.cpp
#include "pch.h"
#include "DepthTarget.h"
#include "Debug.h"

namespace Neuron
{

bool DepthTarget::Create(GraphicsDevice& _device, std::uint32_t _widthPixels, std::uint32_t _heightPixels, DepthTarget& _outTarget) noexcept
{
  NOMAD_ASSERT(_outTarget.m_resource == nullptr);
  NOMAD_ASSERT(_widthPixels > 0 && _heightPixels > 0);
  _outTarget.m_fault = TargetFault::None;
  _outTarget.m_result = S_OK;
  _outTarget.m_device = _device.Device();
  _outTarget.m_widthPixels = _widthPixels;
  _outTarget.m_heightPixels = _heightPixels;

  // R14: no d3dx12.h, so the heap and the resource are written out, as everywhere else in this library.
  const D3D12_HEAP_PROPERTIES heap{.Type = D3D12_HEAP_TYPE_DEFAULT,
                                   .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                   .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
                                   .CreationNodeMask = 1,
                                   .VisibleNodeMask = 1};
  const D3D12_RESOURCE_DESC depth{.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                                  .Alignment = 0,
                                  .Width = _widthPixels,
                                  .Height = _heightPixels,
                                  .DepthOrArraySize = 1,
                                  .MipLevels = 1,
                                  .Format = FORMAT,
                                  // One sample, matching the scene target. ADR-013 is where multisampling both of
                                  // them is decided; until then they agree, which a pipeline requires of them.
                                  .SampleDesc = {1, 0},
                                  .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
                                  .Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL};

  // The optimized clear value must match what Clear uses, for the same reason the scene target's does (NC-021 found
  // that one the hard way, once a frame, in the debug layer).
  D3D12_CLEAR_VALUE clearValue{};
  clearValue.Format = FORMAT;
  clearValue.DepthStencil.Depth = CLEAR_DEPTH;
  clearValue.DepthStencil.Stencil = 0;

  _outTarget.m_result = _outTarget.m_device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &depth, D3D12_RESOURCE_STATE_DEPTH_WRITE,
                                                                     &clearValue, IID_PPV_ARGS(&_outTarget.m_resource));
  if (FAILED(_outTarget.m_result))
  {
    _outTarget.m_fault = TargetFaultFromResult(_outTarget.m_result);
    return false;
  }

  D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
  heapDesc.NumDescriptors = 1;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  heapDesc.NodeMask = 0;
  _outTarget.m_result = _outTarget.m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_outTarget.m_depthStencilHeap));
  if (FAILED(_outTarget.m_result))
  {
    _outTarget.m_fault = TargetFaultFromResult(_outTarget.m_result);
    return false;
  }

  D3D12_DEPTH_STENCIL_VIEW_DESC view{};
  view.Format = FORMAT;
  view.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  view.Flags = D3D12_DSV_FLAG_NONE;
  view.Texture2D.MipSlice = 0;
  _outTarget.m_depthStencilView = _outTarget.m_depthStencilHeap->GetCPUDescriptorHandleForHeapStart();
  _outTarget.m_device->CreateDepthStencilView(_outTarget.m_resource.Get(), &view, _outTarget.m_depthStencilView);
  return true;
}

void DepthTarget::Clear(ID3D12GraphicsCommandList* _commandList) const noexcept
{
  if (_commandList == nullptr || m_resource == nullptr)
  {
    return;
  }
  _commandList->ClearDepthStencilView(m_depthStencilView, D3D12_CLEAR_FLAG_DEPTH, CLEAR_DEPTH, 0, 0, nullptr);
}

} // namespace Neuron
