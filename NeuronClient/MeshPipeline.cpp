// NeuronClient/MeshPipeline.cpp
#include "pch.h"
#include "MeshPipeline.h"
#include "MeshBuilder.h"
#include "PipelineDefaults.h"
#include "Debug.h"
// Build output (AGENTS.md §2): one header per shader, declaring `const BYTE g_<Shader>[]`.
#include "CompiledShaders/MeshVS.h"
#include "CompiledShaders/MeshPS.h"

namespace Neuron
{

namespace
{

using Microsoft::WRL::ComPtr;

constexpr D3D12_INPUT_ELEMENT_DESC VERTEX_LAYOUT[] = {
  {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(MeshVertex, xUnits), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  {"COLOR", 0, DXGI_FORMAT_R32_UINT, 0, offsetof(MeshVertex, colorRgba), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

/// The one thing this pipeline changes about PipelineDefaults: depth on, written, and tested less-than. Everything
/// else — opaque blending, solid fill, one sample — is the default (AGENTS.md §5: a pass sets what it needs).
[[nodiscard]] D3D12_DEPTH_STENCIL_DESC DepthTested() noexcept
{
  D3D12_DEPTH_STENCIL_DESC depth = PipelineDefaults::DepthStencil();
  depth.DepthEnable = TRUE;
  depth.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
  depth.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
  return depth;
}

/// Back faces are culled here, unlike in the 2D passes: a sphere has an inside, and drawing it costs half the pixels
/// for nothing. Counter-clockwise is the front, which is what MeshBuilder's winding produces in a left-handed view.
[[nodiscard]] D3D12_RASTERIZER_DESC BackFaceCulled() noexcept
{
  D3D12_RASTERIZER_DESC rasterizer = PipelineDefaults::Rasterizer();
  rasterizer.CullMode = D3D12_CULL_MODE_BACK;
  rasterizer.FrontCounterClockwise = TRUE;
  return rasterizer;
}

} // namespace

bool MeshPipeline::Create(GraphicsDevice& _device, MeshPipeline& _outPipeline) noexcept
{
  NOMAD_ASSERT(_outPipeline.m_pipeline == nullptr);
  _outPipeline.m_fault = TargetFault::None;
  _outPipeline.m_result = S_OK;
  ID3D12Device* const device = _device.Device();

  D3D12_ROOT_PARAMETER constants{};
  constants.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
  constants.Constants.ShaderRegister = 0;
  constants.Constants.RegisterSpace = 0;
  constants.Constants.Num32BitValues = ROOT_CONSTANT_COUNT;
  constants.ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;

  D3D12_ROOT_SIGNATURE_DESC rootDesc{};
  rootDesc.NumParameters = 1;
  rootDesc.pParameters = &constants;
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

  const D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{.pRootSignature = _outPipeline.m_rootSignature.Get(),
                                                    .VS = {g_MeshVS, sizeof g_MeshVS},
                                                    .PS = {g_MeshPS, sizeof g_MeshPS},
                                                    .BlendState = PipelineDefaults::Blend(),
                                                    .SampleMask = UINT_MAX,
                                                    .RasterizerState = BackFaceCulled(),
                                                    .DepthStencilState = DepthTested(),
                                                    .InputLayout = {VERTEX_LAYOUT, _countof(VERTEX_LAYOUT)},
                                                    .PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
                                                    .NumRenderTargets = 1,
                                                    .RTVFormats = {DXGI_FORMAT_R8G8B8A8_UNORM},
                                                    .DSVFormat = DepthTarget::FORMAT,
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

void MeshPipeline::Begin(ID3D12GraphicsCommandList* _commandList, const Camera& _camera) noexcept
{
  if (_commandList == nullptr || m_pipeline == nullptr)
  {
    return;
  }
  _commandList->SetGraphicsRootSignature(m_rootSignature.Get());
  _commandList->SetPipelineState(m_pipeline.Get());
  const DirectX::XMFLOAT4X4 viewProjection = _camera.ViewProjection();
  _commandList->SetGraphicsRoot32BitConstants(0, 16, &viewProjection, 0);
  SetTint(_commandList, 0xFFFFFFFFu);
}

void MeshPipeline::SetTint(ID3D12GraphicsCommandList* _commandList, std::uint32_t _tintRgba) noexcept
{
  if (_commandList == nullptr || m_pipeline == nullptr)
  {
    return;
  }
  _commandList->SetGraphicsRoot32BitConstants(0, 1, &_tintRgba, 16);
}

} // namespace Neuron
