// NeuronClient/PipelineDefaults.cpp
#include "pch.h"
#include "PipelineDefaults.h"

namespace Neuron
{

D3D12_RASTERIZER_DESC PipelineDefaults::Rasterizer() noexcept
{
  return D3D12_RASTERIZER_DESC{.FillMode = D3D12_FILL_MODE_SOLID,
                               .CullMode = D3D12_CULL_MODE_NONE,
                               .FrontCounterClockwise = FALSE,
                               .DepthBias = D3D12_DEFAULT_DEPTH_BIAS,
                               .DepthBiasClamp = D3D12_DEFAULT_DEPTH_BIAS_CLAMP,
                               .SlopeScaledDepthBias = D3D12_DEFAULT_SLOPE_SCALED_DEPTH_BIAS,
                               .DepthClipEnable = TRUE,
                               .MultisampleEnable = FALSE,
                               .AntialiasedLineEnable = FALSE,
                               .ForcedSampleCount = 0,
                               .ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF};
}

D3D12_BLEND_DESC PipelineDefaults::Blend() noexcept
{
  const D3D12_RENDER_TARGET_BLEND_DESC target{.BlendEnable = FALSE,
                                              .LogicOpEnable = FALSE,
                                              .SrcBlend = D3D12_BLEND_ONE,
                                              .DestBlend = D3D12_BLEND_ZERO,
                                              .BlendOp = D3D12_BLEND_OP_ADD,
                                              .SrcBlendAlpha = D3D12_BLEND_ONE,
                                              .DestBlendAlpha = D3D12_BLEND_ZERO,
                                              .BlendOpAlpha = D3D12_BLEND_OP_ADD,
                                              .LogicOp = D3D12_LOGIC_OP_NOOP,
                                              .RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL};
  return D3D12_BLEND_DESC{.AlphaToCoverageEnable = FALSE,
                          .IndependentBlendEnable = FALSE,
                          .RenderTarget = {target, target, target, target, target, target, target, target}};
}

D3D12_DEPTH_STENCIL_DESC PipelineDefaults::DepthStencil() noexcept
{
  const D3D12_DEPTH_STENCILOP_DESC face{.StencilFailOp = D3D12_STENCIL_OP_KEEP,
                                        .StencilDepthFailOp = D3D12_STENCIL_OP_KEEP,
                                        .StencilPassOp = D3D12_STENCIL_OP_KEEP,
                                        .StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS};
  return D3D12_DEPTH_STENCIL_DESC{.DepthEnable = FALSE,
                                  .DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO,
                                  .DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS,
                                  .StencilEnable = FALSE,
                                  .StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK,
                                  .StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK,
                                  .FrontFace = face,
                                  .BackFace = face};
}

DXGI_SAMPLE_DESC PipelineDefaults::SampleDesc() noexcept
{
  return DXGI_SAMPLE_DESC{.Count = 1, .Quality = 0};
}

} // namespace Neuron
