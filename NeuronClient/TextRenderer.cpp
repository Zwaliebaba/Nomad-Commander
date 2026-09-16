// NeuronClient/TextRenderer.cpp
#include "pch.h"
#include "TextRenderer.h"
#include "BitmapFont.h"
#include "IconAtlas.h"
#include "Debug.h"

#include <array>
#include <cstring>

namespace Neuron
{

namespace
{

using Microsoft::WRL::ComPtr;

constexpr std::uint32_t VERTICES_PER_GLYPH = 6;

/// The codepoint drawn for anything this font does not have: the filled box at the end of the set.
constexpr std::uint32_t REPLACEMENT_CODEPOINT = 0x7F;

/// A row of a texture copy is aligned to this, which is why the upload buffer is wider than the atlas.
constexpr std::uint32_t COPY_ROW_ALIGNMENT = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

[[nodiscard]] D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE _type) noexcept
{
  return D3D12_HEAP_PROPERTIES{.Type = _type,
                               .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                               .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
                               .CreationNodeMask = 1,
                               .VisibleNodeMask = 1};
}

/// Which texel an atlas cell starts at. Sixteen to a row: cells 0 to 95 are the glyphs in codepoint order from 0x20,
/// and 96 onward are the icons (NC-026). One helper, because an icon is the same kind of thing as a glyph.
void AtlasOrigin(std::uint32_t _cellIndex, float& _outTexelX, float& _outTexelY) noexcept
{
  const std::uint32_t index = _cellIndex;
  const std::uint32_t column = index % TextRenderer::ATLAS_COLUMNS;
  const std::uint32_t row = index / TextRenderer::ATLAS_COLUMNS;
  _outTexelX = static_cast<float>(column * TextRenderer::GLYPH_WIDTH_PIXELS);
  _outTexelY = static_cast<float>(row * TextRenderer::GLYPH_HEIGHT_PIXELS);
}

/// Blocks until the queue has passed the value, on a fence and event this function owns. Used once, for the atlas.
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

TextRenderer::~TextRenderer()
{
  if (m_vertexBuffer != nullptr && m_mapped != nullptr)
  {
    m_vertexBuffer->Unmap(0, nullptr);
    m_mapped = nullptr;
  }
}

bool TextRenderer::Create(GraphicsDevice& _device, const GlyphPipeline& _pipeline, TextRenderer& _outRenderer) noexcept
{
  NOMAD_ASSERT(_outRenderer.m_atlas == nullptr);
  _outRenderer.m_fault = TargetFault::None;
  _outRenderer.m_result = S_OK;
  ID3D12Device* const device = _device.Device();

  // The atlas: one texel a pixel, R8_UINT, so the pixel shader reads a 0 or a 1 and nothing has to be decoded at
  // draw time. Six kilobytes of texture for 768 bytes of font is a fine trade for a Load per pixel.
  const D3D12_HEAP_PROPERTIES defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
  const D3D12_RESOURCE_DESC atlasDesc{.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                                      .Alignment = 0,
                                      .Width = ATLAS_WIDTH_TEXELS,
                                      .Height = ATLAS_HEIGHT_TEXELS,
                                      .DepthOrArraySize = 1,
                                      .MipLevels = 1,
                                      .Format = DXGI_FORMAT_R8_UINT,
                                      .SampleDesc = {1, 0},
                                      .Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
                                      .Flags = D3D12_RESOURCE_FLAG_NONE};
  _outRenderer.m_result = device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &atlasDesc, D3D12_RESOURCE_STATE_COPY_DEST,
                                                          nullptr, IID_PPV_ARGS(&_outRenderer.m_atlas));
  if (FAILED(_outRenderer.m_result))
  {
    _outRenderer.m_fault = TargetFaultFromResult(_outRenderer.m_result);
    return false;
  }

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT64 uploadBytes = 0;
  device->GetCopyableFootprints(&atlasDesc, 0, 1, 0, &footprint, nullptr, nullptr, &uploadBytes);
  NOMAD_ASSERT(footprint.Footprint.RowPitch % COPY_ROW_ALIGNMENT == 0);

  const D3D12_HEAP_PROPERTIES uploadHeap = HeapProperties(D3D12_HEAP_TYPE_UPLOAD);
  const D3D12_RESOURCE_DESC uploadDesc{.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
                                       .Alignment = 0,
                                       .Width = uploadBytes,
                                       .Height = 1,
                                       .DepthOrArraySize = 1,
                                       .MipLevels = 1,
                                       .Format = DXGI_FORMAT_UNKNOWN,
                                       .SampleDesc = {1, 0},
                                       .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                       .Flags = D3D12_RESOURCE_FLAG_NONE};
  // A local: it lives until the copy has fenced below and is released on the way out of this function, which is the
  // whole of its life. Nothing keeps an upload buffer for a texture that is written once.
  ComPtr<ID3D12Resource> upload;
  _outRenderer.m_result = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &uploadDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                          nullptr, IID_PPV_ARGS(&upload));
  if (FAILED(_outRenderer.m_result))
  {
    _outRenderer.m_fault = TargetFaultFromResult(_outRenderer.m_result);
    return false;
  }

  void* mappedUpload = nullptr;
  const D3D12_RANGE readsNothing{0, 0};
  _outRenderer.m_result = upload->Map(0, &readsNothing, &mappedUpload);
  if (FAILED(_outRenderer.m_result))
  {
    _outRenderer.m_fault = TargetFaultFromResult(_outRenderer.m_result);
    return false;
  }

  // The bit array becomes one byte a texel, here and once. Bit 7 is the leftmost pixel, which is what BitmapFont.h's
  // binary literals read as on the page.
  std::uint8_t* const rows = static_cast<std::uint8_t*>(mappedUpload);
  std::memset(rows, 0, static_cast<std::size_t>(uploadBytes));
  for (std::uint32_t glyph = 0; glyph < FONT_GLYPH_COUNT; ++glyph)
  {
    const std::uint32_t originX = (glyph % ATLAS_COLUMNS) * GLYPH_WIDTH_PIXELS;
    const std::uint32_t originY = (glyph / ATLAS_COLUMNS) * GLYPH_HEIGHT_PIXELS;
    for (std::uint32_t row = 0; row < FONT_GLYPH_BYTES; ++row)
    {
      const std::uint8_t bits = FONT_8X8_GLYPHS[glyph * FONT_GLYPH_BYTES + row];
      std::uint8_t* const destination = rows + static_cast<std::size_t>(originY + row) * footprint.Footprint.RowPitch + originX;
      for (std::uint32_t column = 0; column < GLYPH_WIDTH_PIXELS; ++column)
      {
        destination[column] = static_cast<std::uint8_t>((bits >> (7 - column)) & 1u);
      }
    }
  }
  // The icons follow the glyphs in the same cell numbering, decoded the same way from the same kind of bit array.
  for (std::uint32_t icon = 0; icon < ICON_COUNT; ++icon)
  {
    const std::uint32_t cell = ICON_FIRST_CELL + icon;
    const std::uint32_t originX = (cell % ATLAS_COLUMNS) * GLYPH_WIDTH_PIXELS;
    const std::uint32_t originY = (cell / ATLAS_COLUMNS) * GLYPH_HEIGHT_PIXELS;
    for (std::uint32_t row = 0; row < ICON_BYTES; ++row)
    {
      const std::uint8_t bits = ICON_8X8_ART[static_cast<std::size_t>(icon) * ICON_BYTES + row];
      std::uint8_t* const destination = rows + static_cast<std::size_t>(originY + row) * footprint.Footprint.RowPitch + originX;
      for (std::uint32_t column = 0; column < GLYPH_WIDTH_PIXELS; ++column)
      {
        destination[column] = static_cast<std::uint8_t>((bits >> (7 - column)) & 1u);
      }
    }
  }
  upload->Unmap(0, nullptr);

  ComPtr<ID3D12CommandAllocator> allocator;
  ComPtr<ID3D12GraphicsCommandList> commandList;
  if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))) ||
      FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList))))
  {
    _outRenderer.m_fault = TargetFault::Allocation;
    return false;
  }

  D3D12_TEXTURE_COPY_LOCATION destination{};
  destination.pResource = _outRenderer.m_atlas.Get();
  destination.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  destination.SubresourceIndex = 0;
  D3D12_TEXTURE_COPY_LOCATION source{};
  source.pResource = upload.Get();
  source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  source.PlacedFootprint = footprint;
  commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);

  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
  barrier.Transition.pResource = _outRenderer.m_atlas.Get();
  barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
  barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  commandList->ResourceBarrier(1, &barrier);

  if (FAILED(commandList->Close()))
  {
    _outRenderer.m_fault = TargetFault::Allocation;
    return false;
  }
  ID3D12CommandList* const lists[] = {commandList.Get()};
  _device.Queue()->ExecuteCommandLists(1, lists);
  if (!WaitForQueue(device, _device.Queue()))
  {
    _outRenderer.m_fault = TargetFault::Allocation;
    return false;
  }

  D3D12_SHADER_RESOURCE_VIEW_DESC view{};
  view.Format = DXGI_FORMAT_R8_UINT;
  view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  view.Texture2D.MostDetailedMip = 0;
  view.Texture2D.MipLevels = 1;
  view.Texture2D.PlaneSlice = 0;
  view.Texture2D.ResourceMinLODClamp = 0.0f;
  device->CreateShaderResourceView(_outRenderer.m_atlas.Get(), &view, _pipeline.CpuHandle(GlyphPipeline::ATLAS_SLOT));

  // The per-frame vertex ring, the same shape as PrimitiveBatch's and for the same reason.
  const UINT64 sliceBytes = static_cast<UINT64>(MAX_GLYPHS_PER_FRAME) * VERTICES_PER_GLYPH * sizeof(GlyphVertex);
  const D3D12_RESOURCE_DESC vertexDesc{.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
                                       .Alignment = 0,
                                       .Width = sliceBytes * FRAMES_IN_FLIGHT,
                                       .Height = 1,
                                       .DepthOrArraySize = 1,
                                       .MipLevels = 1,
                                       .Format = DXGI_FORMAT_UNKNOWN,
                                       .SampleDesc = {1, 0},
                                       .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                       .Flags = D3D12_RESOURCE_FLAG_NONE};
  _outRenderer.m_result = device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &vertexDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                          nullptr, IID_PPV_ARGS(&_outRenderer.m_vertexBuffer));
  if (FAILED(_outRenderer.m_result))
  {
    _outRenderer.m_fault = TargetFaultFromResult(_outRenderer.m_result);
    return false;
  }
  void* mappedVertices = nullptr;
  _outRenderer.m_result = _outRenderer.m_vertexBuffer->Map(0, &readsNothing, &mappedVertices);
  if (FAILED(_outRenderer.m_result))
  {
    _outRenderer.m_fault = TargetFaultFromResult(_outRenderer.m_result);
    return false;
  }
  _outRenderer.m_mapped = static_cast<GlyphVertex*>(mappedVertices);
  _outRenderer.m_bufferAddress = _outRenderer.m_vertexBuffer->GetGPUVirtualAddress();
  return true;
}

void TextRenderer::Begin(ID3D12GraphicsCommandList* _commandList, const GlyphPipeline& _pipeline, std::uint32_t _frameSlot,
                         std::uint32_t _targetWidthPixels, std::uint32_t _targetHeightPixels) noexcept
{
  NOMAD_ASSERT(_frameSlot < FRAMES_IN_FLIGHT);
  m_commandList = _commandList;
  m_pipeline = &_pipeline;
  m_frameSlot = _frameSlot < FRAMES_IN_FLIGHT ? _frameSlot : 0;
  m_targetWidthPixels = _targetWidthPixels;
  m_targetHeightPixels = _targetHeightPixels;
  m_glyphCount = 0;
  m_overflowed = false;
}

TextExtent TextRenderer::Measure(std::string_view _text, std::uint32_t _scale) noexcept
{
  if (_text.empty() || _scale == 0)
  {
    return TextExtent{0, 0};
  }
  return TextExtent{static_cast<std::uint32_t>(_text.size()) * GLYPH_WIDTH_PIXELS * _scale, GLYPH_HEIGHT_PIXELS * _scale};
}

void TextRenderer::Draw(float _xPixels, float _yPixels, std::string_view _text, std::uint32_t _colorRgba, std::uint32_t _scale) noexcept
{
  if (m_mapped == nullptr || _scale == 0)
  {
    return;
  }
  const float cellWidth = static_cast<float>(GLYPH_WIDTH_PIXELS * _scale);
  const float cellHeight = static_cast<float>(GLYPH_HEIGHT_PIXELS * _scale);

  for (std::size_t index = 0; index < _text.size(); ++index)
  {
    const auto character = static_cast<unsigned char>(_text[index]);
    std::uint32_t codepoint = character;
    if (codepoint < FONT_FIRST_CODEPOINT || codepoint >= FONT_FIRST_CODEPOINT + FONT_GLYPH_COUNT)
    {
      codepoint = REPLACEMENT_CODEPOINT;
    }
    float texelX = 0.0f;
    float texelY = 0.0f;
    AtlasOrigin(codepoint - FONT_FIRST_CODEPOINT, texelX, texelY);
    PushQuad(_xPixels + static_cast<float>(index) * cellWidth, _yPixels, cellWidth, cellHeight, texelX, texelY, _colorRgba);
  }
}

void TextRenderer::DrawIcon(float _xPixels, float _yPixels, Icon _icon, std::uint32_t _colorRgba, std::uint32_t _scale) noexcept
{
  if (m_mapped == nullptr || _scale == 0 || static_cast<std::uint32_t>(_icon) >= ICON_COUNT)
  {
    return;
  }
  float texelX = 0.0f;
  float texelY = 0.0f;
  AtlasOrigin(ICON_FIRST_CELL + static_cast<std::uint32_t>(_icon), texelX, texelY);
  PushQuad(_xPixels, _yPixels, static_cast<float>(GLYPH_WIDTH_PIXELS * _scale), static_cast<float>(GLYPH_HEIGHT_PIXELS * _scale), texelX,
           texelY, _colorRgba);
}

void TextRenderer::PushQuad(float _leftPixels, float _topPixels, float _widthPixels, float _heightPixels, float _texelX, float _texelY,
                            std::uint32_t _colorRgba) noexcept
{
  if (m_glyphCount == MAX_GLYPHS_PER_FRAME)
  {
    m_overflowed = true;
    return;
  }
  GlyphVertex* const slice = m_mapped + static_cast<std::size_t>(m_frameSlot) * MAX_GLYPHS_PER_FRAME * VERTICES_PER_GLYPH;
  const float right = _leftPixels + _widthPixels;
  const float bottom = _topPixels + _heightPixels;
  const float texelRight = _texelX + static_cast<float>(GLYPH_WIDTH_PIXELS);
  const float texelBottom = _texelY + static_cast<float>(GLYPH_HEIGHT_PIXELS);

  GlyphVertex* const vertices = slice + static_cast<std::size_t>(m_glyphCount) * VERTICES_PER_GLYPH;
  vertices[0] = {_leftPixels, _topPixels, _texelX, _texelY, _colorRgba};
  vertices[1] = {right, _topPixels, texelRight, _texelY, _colorRgba};
  vertices[2] = {_leftPixels, bottom, _texelX, texelBottom, _colorRgba};
  vertices[3] = {right, _topPixels, texelRight, _texelY, _colorRgba};
  vertices[4] = {right, bottom, texelRight, texelBottom, _colorRgba};
  vertices[5] = {_leftPixels, bottom, _texelX, texelBottom, _colorRgba};
  ++m_glyphCount;
}

void TextRenderer::End() noexcept
{
  if (m_commandList == nullptr || m_pipeline == nullptr || m_glyphCount == 0)
  {
    m_commandList = nullptr;
    m_pipeline = nullptr;
    return;
  }

  D3D12_VERTEX_BUFFER_VIEW view{};
  view.BufferLocation = m_bufferAddress + static_cast<UINT64>(m_frameSlot) * MAX_GLYPHS_PER_FRAME *
                                            static_cast<UINT64>(VERTICES_PER_GLYPH) * sizeof(GlyphVertex);
  view.SizeInBytes = static_cast<UINT>(static_cast<std::size_t>(m_glyphCount) * VERTICES_PER_GLYPH * sizeof(GlyphVertex));
  view.StrideInBytes = sizeof(GlyphVertex);

  ID3D12DescriptorHeap* const heaps[] = {m_pipeline->DescriptorHeap()};
  m_commandList->SetDescriptorHeaps(1, heaps);
  m_commandList->SetGraphicsRootSignature(m_pipeline->RootSignature());
  const std::uint32_t screenSize[GlyphPipeline::ROOT_CONSTANT_COUNT] = {m_targetWidthPixels, m_targetHeightPixels};
  m_commandList->SetGraphicsRoot32BitConstants(0, GlyphPipeline::ROOT_CONSTANT_COUNT, screenSize, 0);
  m_commandList->SetGraphicsRootDescriptorTable(1, m_pipeline->GpuHandle(GlyphPipeline::ATLAS_SLOT));
  m_commandList->SetPipelineState(m_pipeline->Pipeline());
  m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  m_commandList->IASetVertexBuffers(0, 1, &view);
  m_commandList->DrawInstanced(m_glyphCount * VERTICES_PER_GLYPH, 1, 0, 0);

  m_commandList = nullptr;
  m_pipeline = nullptr;
}

} // namespace Neuron
