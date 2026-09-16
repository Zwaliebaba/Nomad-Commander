// NeuronClient/PresentPass.cpp
#include "pch.h"
#include "PresentPass.h"
#include "PipelineDefaults.h"
#include "Debug.h"
// Build output (AGENTS.md §2): one header per shader, declaring `const BYTE g_<Shader>[]`.
#include "CompiledShaders/PresentVS.h"
#include "CompiledShaders/PresentPS.h"

namespace Neuron
{

namespace
{

using Microsoft::WRL::ComPtr;

/// The root constant that tells the pixel shader which of its two static samplers to use.
constexpr UINT LINEAR_FILTER_OFF = 0;
constexpr UINT LINEAR_FILTER_ON = 1;

/// Three vertices, generated from SV_VertexID: one triangle that covers the viewport.
constexpr UINT FULLSCREEN_TRIANGLE_VERTICES = 3;

/// Records a transition of the back buffer. The swap chain's own helper is private to it, and this is the one other
/// place that has to move that resource -- into COPY_DEST for the 1:1 path and back.
void TransitionResource(ID3D12GraphicsCommandList* _commandList, ID3D12Resource* _resource, D3D12_RESOURCE_STATES _before,
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

/// One static sampler. Samplers are a pass's own business (AGENTS.md §5), so PipelineDefaults holds none and this
/// stays here; the blend, rasterizer and depth-stencil states this pass used to spell out for itself now come from
/// PipelineDefaults, which NC-022 built out of exactly these.
///
/// Every field is named rather than zero-initialized and patched, because D3D12_TEXTURE_ADDRESS_MODE has no
/// zero-valued enumerator -- so `D3D12_STATIC_SAMPLER_DESC sampler{}` would hold a value the enum has no name for.
[[nodiscard]] D3D12_STATIC_SAMPLER_DESC StaticSampler(UINT _shaderRegister, D3D12_FILTER _filter) noexcept
{
  return D3D12_STATIC_SAMPLER_DESC{.Filter = _filter,
                                   // Clamp, because the triangle's texture coordinate reaches 2 before clipping and a
                                   // wrapped edge would show as the opposite edge bleeding into the first and last
                                   // row of pixels.
                                   .AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                   .AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                   .AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
                                   .MipLODBias = 0.0f,
                                   .MaxAnisotropy = 1,
                                   .ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER,
                                   .BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK,
                                   .MinLOD = 0.0f,
                                   .MaxLOD = 0.0f,
                                   .ShaderRegister = _shaderRegister,
                                   .RegisterSpace = 0,
                                   .ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL};
}

} // namespace

PresentPass::Placement PresentPass::Fit(std::uint32_t _sceneWidthPixels, std::uint32_t _sceneHeightPixels, std::uint32_t _clientWidthPixels,
                                        std::uint32_t _clientHeightPixels) noexcept
{
  Placement placement{};
  if (_sceneWidthPixels == 0 || _sceneHeightPixels == 0 || _clientWidthPixels == 0 || _clientHeightPixels == 0)
  {
    return placement;
  }

  // ADR-009's first case. A copy, not a sampler that lands on texel centres: the guarantee is that no filter runs.
  if (_clientWidthPixels == _sceneWidthPixels && _clientHeightPixels == _sceneHeightPixels)
  {
    placement.widthPixels = _sceneWidthPixels;
    placement.heightPixels = _sceneHeightPixels;
    placement.filter = Filter::None;
    return placement;
  }

  // ADR-009's second case: the client area is the screen an exact whole number of times, on both axes, by the same
  // factor -- so it fills exactly and every texel is a square block of pixels.
  if (_clientWidthPixels % _sceneWidthPixels == 0 && _clientHeightPixels % _sceneHeightPixels == 0 &&
      _clientWidthPixels / _sceneWidthPixels == _clientHeightPixels / _sceneHeightPixels)
  {
    placement.widthPixels = _clientWidthPixels;
    placement.heightPixels = _clientHeightPixels;
    placement.filter = Filter::Point;
    return placement;
  }

  // ADR-009's third case: the largest rectangle of the screen's shape that fits, centred, the rest left as bars.
  // Integer arithmetic in 64 bits, because the products are of two screen extents -- and because a float here would
  // put a rounding rule nobody chose between the window and what a person sees.
  const std::uint64_t widthIfHeightBound = static_cast<std::uint64_t>(_clientHeightPixels) * _sceneWidthPixels / _sceneHeightPixels;
  if (widthIfHeightBound <= _clientWidthPixels)
  {
    placement.widthPixels = static_cast<std::uint32_t>(widthIfHeightBound);
    placement.heightPixels = _clientHeightPixels;
  }
  else
  {
    placement.widthPixels = _clientWidthPixels;
    placement.heightPixels =
      static_cast<std::uint32_t>(static_cast<std::uint64_t>(_clientWidthPixels) * _sceneHeightPixels / _sceneWidthPixels);
  }
  placement.leftPixels = static_cast<std::int32_t>(_clientWidthPixels - placement.widthPixels) / 2;
  placement.topPixels = static_cast<std::int32_t>(_clientHeightPixels - placement.heightPixels) / 2;
  placement.filter = Filter::Linear;
  return placement;
}

bool PresentPass::Create(GraphicsDevice& _device, const SceneTarget& _scene, PresentPass& _outPass) noexcept
{
  NOMAD_ASSERT(_outPass.m_pipeline == nullptr);
  NOMAD_ASSERT(_scene.Resource() != nullptr);
  _outPass.m_fault = TargetFault::None;
  _outPass.m_result = S_OK;
  ID3D12Device* const device = _device.Device();

  // One descriptor: the scene target as a shader resource. Shader-visible, because the pixel shader reads it.
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.NumDescriptors = 1;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  heapDesc.NodeMask = 0;
  _outPass.m_result = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_outPass.m_sceneDescriptorHeap));
  if (FAILED(_outPass.m_result))
  {
    _outPass.m_fault = TargetFaultFromResult(_outPass.m_result);
    return false;
  }

  D3D12_SHADER_RESOURCE_VIEW_DESC view{};
  view.Format = _scene.ColorFormat();
  view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  view.Texture2D.MostDetailedMip = 0;
  view.Texture2D.MipLevels = 1;
  view.Texture2D.PlaneSlice = 0;
  view.Texture2D.ResourceMinLODClamp = 0.0f;
  device->CreateShaderResourceView(_scene.Resource(), &view, _outPass.m_sceneDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

  D3D12_DESCRIPTOR_RANGE sceneRange{};
  sceneRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  sceneRange.NumDescriptors = 1;
  sceneRange.BaseShaderRegister = 0;
  sceneRange.RegisterSpace = 0;
  sceneRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[0].Constants.ShaderRegister = 0;
  parameters[0].Constants.RegisterSpace = 0;
  parameters[0].Constants.Num32BitValues = 1;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  parameters[1].DescriptorTable.pDescriptorRanges = &sceneRange;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  const D3D12_STATIC_SAMPLER_DESC samplers[2] = {StaticSampler(0, D3D12_FILTER_MIN_MAG_MIP_POINT),
                                                 StaticSampler(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR)};

  D3D12_ROOT_SIGNATURE_DESC rootDesc{};
  rootDesc.NumParameters = _countof(parameters);
  rootDesc.pParameters = parameters;
  rootDesc.NumStaticSamplers = _countof(samplers);
  rootDesc.pStaticSamplers = samplers;
  // No input layout is allowed, because there is no vertex buffer: the triangle comes from SV_VertexID. Every stage
  // this pass does not use is denied, which is what lets the driver skip work for them.
  rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                   D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

  ComPtr<ID3DBlob> serialized;
  ComPtr<ID3DBlob> errors;
  _outPass.m_result = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &serialized, &errors);
  if (FAILED(_outPass.m_result))
  {
    if (errors != nullptr)
    {
      DebugPrint(static_cast<const char*>(errors->GetBufferPointer()));
    }
    _outPass.m_fault = TargetFaultFromResult(_outPass.m_result);
    return false;
  }
  _outPass.m_result =
    device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&_outPass.m_rootSignature));
  if (FAILED(_outPass.m_result))
  {
    _outPass.m_fault = TargetFaultFromResult(_outPass.m_result);
    return false;
  }

  // No input layout and no vertex buffer: the triangle comes from SV_VertexID. The fields left out below carry no
  // enum that lacks a zero value, which is why they may be defaulted where the three states above may not.
  const D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{.pRootSignature = _outPass.m_rootSignature.Get(),
                                                    .VS = {g_PresentVS, sizeof g_PresentVS},
                                                    .PS = {g_PresentPS, sizeof g_PresentPS},
                                                    .BlendState = PipelineDefaults::Blend(),
                                                    .SampleMask = UINT_MAX,
                                                    .RasterizerState = PipelineDefaults::Rasterizer(),
                                                    .DepthStencilState = PipelineDefaults::DepthStencil(),
                                                    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
                                                    .NumRenderTargets = 1,
                                                    .RTVFormats = {DXGI_FORMAT_R8G8B8A8_UNORM},
                                                    .DSVFormat = DXGI_FORMAT_UNKNOWN,
                                                    .SampleDesc = PipelineDefaults::SampleDesc(),
                                                    .NodeMask = 0,
                                                    .Flags = D3D12_PIPELINE_STATE_FLAG_NONE};

  _outPass.m_result = device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&_outPass.m_pipeline));
  if (FAILED(_outPass.m_result))
  {
    _outPass.m_fault = TargetFaultFromResult(_outPass.m_result);
    return false;
  }
  return true;
}

void PresentPass::Execute(ID3D12GraphicsCommandList* _commandList, SceneTarget& _scene, SwapChainTarget& _swapChain) noexcept
{
  // The frame's own call. A back buffer's extent IS the client area ADR-009 fits against, so there is nothing to
  // decide here beyond naming the two.
  Execute(_commandList, _scene, _swapChain.BackBuffer(), _swapChain.WidthPixels(), _swapChain.HeightPixels());
}

void PresentPass::Execute(ID3D12GraphicsCommandList* _commandList, SceneTarget& _scene, ID3D12Resource* _destination,
                          std::uint32_t _destinationWidthPixels, std::uint32_t _destinationHeightPixels) noexcept
{
  if (_commandList == nullptr || m_pipeline == nullptr || _destination == nullptr)
  {
    return;
  }
  const Placement placement = Fit(_scene.WidthPixels(), _scene.HeightPixels(), _destinationWidthPixels, _destinationHeightPixels);
  m_lastPlacement = placement;
  if (placement.widthPixels == 0 || placement.heightPixels == 0)
  {
    return;
  }

  if (placement.filter == Filter::None)
  {
    // The cheapest path there is, and the only one that is unfiltered by construction rather than by argument.
    _scene.Transition(_commandList, D3D12_RESOURCE_STATE_COPY_SOURCE);
    TransitionResource(_commandList, _destination, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_DEST);
    _commandList->CopyResource(_destination, _scene.Resource());
    TransitionResource(_commandList, _destination, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET);
    _scene.Transition(_commandList, D3D12_RESOURCE_STATE_RENDER_TARGET);
    return;
  }

  _scene.Transition(_commandList, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

  D3D12_VIEWPORT viewport{};
  viewport.TopLeftX = static_cast<FLOAT>(placement.leftPixels);
  viewport.TopLeftY = static_cast<FLOAT>(placement.topPixels);
  viewport.Width = static_cast<FLOAT>(placement.widthPixels);
  viewport.Height = static_cast<FLOAT>(placement.heightPixels);
  viewport.MinDepth = 0.0f;
  viewport.MaxDepth = 1.0f;
  D3D12_RECT scissor{};
  scissor.left = placement.leftPixels;
  scissor.top = placement.topPixels;
  scissor.right = placement.leftPixels + static_cast<LONG>(placement.widthPixels);
  scissor.bottom = placement.topPixels + static_cast<LONG>(placement.heightPixels);

  ID3D12DescriptorHeap* const heaps[] = {m_sceneDescriptorHeap.Get()};
  _commandList->SetDescriptorHeaps(1, heaps);
  _commandList->SetGraphicsRootSignature(m_rootSignature.Get());
  _commandList->SetGraphicsRoot32BitConstant(0, placement.filter == Filter::Linear ? LINEAR_FILTER_ON : LINEAR_FILTER_OFF, 0);
  _commandList->SetGraphicsRootDescriptorTable(1, m_sceneDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
  _commandList->SetPipelineState(m_pipeline.Get());
  _commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _commandList->RSSetViewports(1, &viewport);
  _commandList->RSSetScissorRects(1, &scissor);
  _commandList->DrawInstanced(FULLSCREEN_TRIANGLE_VERTICES, 1, 0, 0);

  _scene.Transition(_commandList, D3D12_RESOURCE_STATE_RENDER_TARGET);
}

} // namespace Neuron
