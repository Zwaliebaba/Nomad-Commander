// NeuronClient/PrimitivePipeline.cpp
#include "pch.h"
#include "PrimitivePipeline.h"
#include "PipelineDefaults.h"
#include "PrimitiveBatch.h"
#include "Debug.h"
// Build output (AGENTS.md §2): one header per shader, declaring `const BYTE g_<Shader>[]`. This is the one translation
// unit that includes them. BYTE comes from <windows.h>, which pch.h brings in through NeuronCore.h.
#include "CompiledShaders/PrimitiveVS.h"
#include "CompiledShaders/PrimitivePS.h"

namespace Neuron
{

namespace
{

using Microsoft::WRL::ComPtr;

/// The vertex layout, which is PrimitiveVertex's three fields and nothing else. R32_UINT rather than R8G8B8A8_UNORM
/// for the colour: the shader is what unpacks it, so the bytes travel as the bit pattern they were authored as.
constexpr D3D12_INPUT_ELEMENT_DESC VERTEX_LAYOUT[] = {
  {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(PrimitiveVertex, positionXPixels), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  {"COLOR", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(PrimitiveVertex, colorRgba), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

/// The two pipelines differ in exactly one field, so they are built from one description.
[[nodiscard]] D3D12_GRAPHICS_PIPELINE_STATE_DESC PipelineDescription(ID3D12RootSignature* _rootSignature,
                                                                     D3D12_PRIMITIVE_TOPOLOGY_TYPE _topology) noexcept
{
  return D3D12_GRAPHICS_PIPELINE_STATE_DESC{.pRootSignature = _rootSignature,
                                            .VS = {g_PrimitiveVS, sizeof g_PrimitiveVS},
                                            .PS = {g_PrimitivePS, sizeof g_PrimitivePS},
                                            .BlendState = PipelineDefaults::Blend(),
                                            .SampleMask = UINT_MAX,
                                            .RasterizerState = PipelineDefaults::Rasterizer(),
                                            .DepthStencilState = PipelineDefaults::DepthStencil(),
                                            .InputLayout = {VERTEX_LAYOUT, _countof(VERTEX_LAYOUT)},
                                            .PrimitiveTopologyType = _topology,
                                            .NumRenderTargets = 1,
                                            .RTVFormats = {DXGI_FORMAT_R8G8B8A8_UNORM},
                                            .DSVFormat = DXGI_FORMAT_UNKNOWN,
                                            .SampleDesc = PipelineDefaults::SampleDesc(),
                                            .NodeMask = 0,
                                            .Flags = D3D12_PIPELINE_STATE_FLAG_NONE};
}

} // namespace

std::span<const std::byte> PrimitiveVertexShader() noexcept
{
  return std::as_bytes(std::span{g_PrimitiveVS});
}

std::span<const std::byte> PrimitivePixelShader() noexcept
{
  return std::as_bytes(std::span{g_PrimitivePS});
}

bool PrimitivePipeline::Create(GraphicsDevice& _device, PrimitivePipeline& _outPipeline) noexcept
{
  NOMAD_ASSERT(_outPipeline.m_trianglePipeline == nullptr);
  _outPipeline.m_fault = TargetFault::None;
  _outPipeline.m_result = S_OK;
  ID3D12Device* const device = _device.Device();

  // Root constants only: the screen size, read by the vertex shader. No table, no descriptor heap, nothing to upload.
  D3D12_ROOT_PARAMETER screenSize{};
  screenSize.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  screenSize.Constants.ShaderRegister = 0;
  screenSize.Constants.RegisterSpace = 0;
  screenSize.Constants.Num32BitValues = ROOT_CONSTANT_COUNT;
  screenSize.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

  D3D12_ROOT_SIGNATURE_DESC rootDesc{};
  rootDesc.NumParameters = 1;
  rootDesc.pParameters = &screenSize;
  rootDesc.NumStaticSamplers = 0;
  rootDesc.pStaticSamplers = nullptr;
  rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                   D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                   D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;

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

  const D3D12_GRAPHICS_PIPELINE_STATE_DESC triangles =
    PipelineDescription(_outPipeline.m_rootSignature.Get(), D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
  _outPipeline.m_result = device->CreateGraphicsPipelineState(&triangles, IID_PPV_ARGS(&_outPipeline.m_trianglePipeline));
  if (FAILED(_outPipeline.m_result))
  {
    _outPipeline.m_fault = TargetFaultFromResult(_outPipeline.m_result);
    return false;
  }

  const D3D12_GRAPHICS_PIPELINE_STATE_DESC lines =
    PipelineDescription(_outPipeline.m_rootSignature.Get(), D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE);
  _outPipeline.m_result = device->CreateGraphicsPipelineState(&lines, IID_PPV_ARGS(&_outPipeline.m_linePipeline));
  if (FAILED(_outPipeline.m_result))
  {
    _outPipeline.m_fault = TargetFaultFromResult(_outPipeline.m_result);
    return false;
  }
  return true;
}

} // namespace Neuron
