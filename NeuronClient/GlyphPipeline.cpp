// NeuronClient/GlyphPipeline.cpp
#include "pch.h"
#include "GlyphPipeline.h"
#include "PipelineDefaults.h"
#include "TextRenderer.h"
#include "Debug.h"
// Build output (AGENTS.md §2): one header per shader, declaring `const BYTE g_<Shader>[]`.
#include "CompiledShaders/GlyphVS.h"
#include "CompiledShaders/GlyphPS.h"

namespace Neuron
{

namespace
{

using Microsoft::WRL::ComPtr;

/// The vertex layout, which is GlyphVertex's fields. The texel coordinate is a float pair so that it interpolates;
/// the pixel shader is what turns it back into an integer, and that is where the exact scaling comes from.
constexpr D3D12_INPUT_ELEMENT_DESC VERTEX_LAYOUT[] = {
  {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(GlyphVertex, positionXPixels), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(GlyphVertex, texelX), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  {"COLOR", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(GlyphVertex, colorRgba), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

} // namespace

bool GlyphPipeline::Create(GraphicsDevice& _device, GlyphPipeline& _outPipeline) noexcept
{
  NOMAD_ASSERT(_outPipeline.m_pipeline == nullptr);
  _outPipeline.m_fault = TargetFault::None;
  _outPipeline.m_result = S_OK;
  ID3D12Device* const device = _device.Device();

  D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.NumDescriptors = DESCRIPTOR_SLOTS;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  heapDesc.NodeMask = 0;
  _outPipeline.m_result = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_outPipeline.m_descriptorHeap));
  if (FAILED(_outPipeline.m_result))
  {
    _outPipeline.m_fault = TargetFaultFromResult(_outPipeline.m_result);
    return false;
  }
  _outPipeline.m_descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  D3D12_DESCRIPTOR_RANGE atlasRange{};
  atlasRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  atlasRange.NumDescriptors = 1;
  atlasRange.BaseShaderRegister = 0;
  atlasRange.RegisterSpace = 0;
  atlasRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

  D3D12_ROOT_PARAMETER parameters[2] = {};
  parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  parameters[0].Constants.ShaderRegister = 0;
  parameters[0].Constants.RegisterSpace = 0;
  parameters[0].Constants.Num32BitValues = ROOT_CONSTANT_COUNT;
  parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
  parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  parameters[1].DescriptorTable.NumDescriptorRanges = 1;
  parameters[1].DescriptorTable.pDescriptorRanges = &atlasRange;
  parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

  D3D12_ROOT_SIGNATURE_DESC rootDesc{};
  rootDesc.NumParameters = _countof(parameters);
  rootDesc.pParameters = parameters;
  // No static sampler, and none anywhere in this pass: the glyph path Loads texels rather than sampling them.
  rootDesc.NumStaticSamplers = 0;
  rootDesc.pStaticSamplers = nullptr;
  rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                   D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;

  ComPtr<ID3DBlob> serialized;
  ComPtr<ID3DBlob> errors;
  _outPipeline.m_result = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1_0, &serialized, &errors);
  if (FAILED(_outPipeline.m_result))
  {
    if (errors != nullptr)
    {
      DebugPrint(static_cast<const char*>(errors->GetBufferPointer()));
    }
    _outPipeline.m_fault = TargetFaultFromResult(_outPipeline.m_result);
    return false;
  }
  _outPipeline.m_result = device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                                      IID_PPV_ARGS(&_outPipeline.m_rootSignature));
  if (FAILED(_outPipeline.m_result))
  {
    _outPipeline.m_fault = TargetFaultFromResult(_outPipeline.m_result);
    return false;
  }

  const D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{.pRootSignature = _outPipeline.m_rootSignature.Get(),
                                                    .VS = {g_GlyphVS, sizeof g_GlyphVS},
                                                    .PS = {g_GlyphPS, sizeof g_GlyphPS},
                                                    .BlendState = PipelineDefaults::Blend(),
                                                    .SampleMask = UINT_MAX,
                                                    .RasterizerState = PipelineDefaults::Rasterizer(),
                                                    .DepthStencilState = PipelineDefaults::DepthStencil(),
                                                    .InputLayout = {VERTEX_LAYOUT, _countof(VERTEX_LAYOUT)},
                                                    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
                                                    .NumRenderTargets = 1,
                                                    .RTVFormats = {DXGI_FORMAT_R8G8B8A8_UNORM},
                                                    .DSVFormat = DXGI_FORMAT_UNKNOWN,
                                                    .SampleDesc = PipelineDefaults::SampleDesc(),
                                                    .NodeMask = 0,
                                                    .Flags = D3D12_PIPELINE_STATE_FLAG_NONE};
  _outPipeline.m_result = device->CreateGraphicsPipelineState(&pipeline, IID_PPV_ARGS(&_outPipeline.m_pipeline));
  if (FAILED(_outPipeline.m_result))
  {
    _outPipeline.m_fault = TargetFaultFromResult(_outPipeline.m_result);
    return false;
  }
  return true;
}

D3D12_CPU_DESCRIPTOR_HANDLE GlyphPipeline::CpuHandle(UINT _slot) const noexcept
{
  NOMAD_ASSERT(_slot < DESCRIPTOR_SLOTS);
  D3D12_CPU_DESCRIPTOR_HANDLE handle = m_descriptorHeap->GetCPUDescriptorHandleForHeapStart();
  handle.ptr += static_cast<SIZE_T>(_slot) * m_descriptorSize;
  return handle;
}

D3D12_GPU_DESCRIPTOR_HANDLE GlyphPipeline::GpuHandle(UINT _slot) const noexcept
{
  NOMAD_ASSERT(_slot < DESCRIPTOR_SLOTS);
  D3D12_GPU_DESCRIPTOR_HANDLE handle = m_descriptorHeap->GetGPUDescriptorHandleForHeapStart();
  handle.ptr += static_cast<UINT64>(_slot) * m_descriptorSize;
  return handle;
}

} // namespace Neuron
