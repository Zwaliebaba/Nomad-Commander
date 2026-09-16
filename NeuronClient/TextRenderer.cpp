// NeuronClient/TextRenderer.cpp
#include "pch.h"
#include "TextRenderer.h"
#include "Font.h"
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

/// The codepoint drawn for anything the faces do not hold: the box at the end of the set.
constexpr std::uint32_t REPLACEMENT_CODEPOINT = 0x7F;

/// A row of a texture copy is aligned to this, which is why the upload buffer is wider than the atlas.
constexpr std::uint32_t COPY_ROW_ALIGNMENT = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;

/// What a set bit of icon art becomes: a texel the pixel shader blends at one, which is the text colour exactly.
constexpr std::uint8_t FULL_COVERAGE = 0xFF;

/// The faces in `Font` order, which is the order they are stacked in the atlas.
constexpr std::array<Font, FONT_COUNT> FACES = {Font::Body, Font::Small, Font::Title};

[[nodiscard]] D3D12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE _type) noexcept
{
  return D3D12_HEAP_PROPERTIES{.Type = _type,
                               .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                               .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
                               .CreationNodeMask = 1,
                               .VisibleNodeMask = 1};
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

void TextRenderer::GlyphOrigin(Font _font, std::uint32_t _glyphIndex, std::uint32_t& _outTexelX, std::uint32_t& _outTexelY) noexcept
{
  NOMAD_ASSERT(_glyphIndex < FONT_GLYPH_COUNT);
  const FontMetrics metrics = MetricsOf(_font);
  const std::uint32_t column = _glyphIndex % ATLAS_COLUMNS;
  const std::uint32_t row = _glyphIndex / ATLAS_COLUMNS;
  _outTexelX = column * metrics.advancePixels;
  _outTexelY = static_cast<std::uint32_t>(_font) * FACE_HEIGHT_TEXELS + row * FONT_LINE_HEIGHT_PIXELS;
}

bool TextRenderer::Create(GraphicsDevice& _device, const GlyphPipeline& _pipeline, TextRenderer& _outRenderer) noexcept
{
  NOMAD_ASSERT(_outRenderer.m_atlas == nullptr);
  _outRenderer.m_fault = TargetFault::None;
  _outRenderer.m_result = S_OK;
  ID3D12Device* const device = _device.Device();

  // The atlas: one texel a pixel, R8_UNORM, so the pixel shader Loads a coverage in [0, 1] and blends by it with
  // nothing to decode at draw time.
  const D3D12_HEAP_PROPERTIES defaultHeap = HeapProperties(D3D12_HEAP_TYPE_DEFAULT);
  const D3D12_RESOURCE_DESC atlasDesc{.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                                      .Alignment = 0,
                                      .Width = ATLAS_WIDTH_TEXELS,
                                      .Height = ATLAS_HEIGHT_TEXELS,
                                      .DepthOrArraySize = 1,
                                      .MipLevels = 1,
                                      .Format = DXGI_FORMAT_R8_UNORM,
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

  std::uint8_t* const rows = static_cast<std::uint8_t*>(mappedUpload);
  std::memset(rows, 0, static_cast<std::size_t>(uploadBytes));
  // The faces: each glyph's rows copied into its cell, one byte a texel, exactly as Tools/BakeFont.py left them.
  for (const Font font : FACES)
  {
    const FontMetrics metrics = MetricsOf(font);
    const std::size_t glyphBytes = static_cast<std::size_t>(metrics.advancePixels) * FONT_LINE_HEIGHT_PIXELS;
    for (std::uint32_t glyph = 0; glyph < FONT_GLYPH_COUNT; ++glyph)
    {
      std::uint32_t originX = 0;
      std::uint32_t originY = 0;
      GlyphOrigin(font, glyph, originX, originY);
      const std::uint8_t* const source = metrics.coverage + static_cast<std::size_t>(glyph) * glyphBytes;
      for (std::uint32_t row = 0; row < FONT_LINE_HEIGHT_PIXELS; ++row)
      {
        std::memcpy(rows + static_cast<std::size_t>(originY + row) * footprint.Footprint.RowPitch + originX,
                    source + static_cast<std::size_t>(row) * metrics.advancePixels, metrics.advancePixels);
      }
    }
  }
  // The icons: 8×8 bits become a cell of full or no coverage, three texels a bit, so the art stays the pixel art it
  // was drawn as and lands on the glass as exactly that.
  for (std::uint32_t icon = 0; icon < ICON_COUNT; ++icon)
  {
    const std::uint32_t originX = icon * ICON_PIXELS;
    for (std::uint32_t bitRow = 0; bitRow < ICON_ART_PIXELS; ++bitRow)
    {
      const std::uint8_t bits = ICON_8X8_ART[static_cast<std::size_t>(icon) * ICON_BYTES + bitRow];
      for (std::uint32_t bitColumn = 0; bitColumn < ICON_ART_PIXELS; ++bitColumn)
      {
        if (((bits >> (ICON_ART_PIXELS - 1 - bitColumn)) & 1u) == 0)
        {
          continue;
        }
        const std::size_t texelColumn = originX + static_cast<std::size_t>(bitColumn) * ICON_TEXELS_PER_BIT;
        for (std::uint32_t sub = 0; sub < ICON_TEXELS_PER_BIT; ++sub)
        {
          const std::size_t texelRow = ICONS_ORIGIN_Y_TEXELS + static_cast<std::size_t>(bitRow) * ICON_TEXELS_PER_BIT + sub;
          std::memset(rows + texelRow * footprint.Footprint.RowPitch + texelColumn, FULL_COVERAGE, ICON_TEXELS_PER_BIT);
        }
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
  view.Format = DXGI_FORMAT_R8_UNORM;
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

TextExtent TextRenderer::Measure(std::string_view _text, Font _font) noexcept
{
  if (_text.empty())
  {
    return TextExtent{0, 0};
  }
  return TextExtent{static_cast<std::uint32_t>(_text.size()) * MetricsOf(_font).advancePixels, FONT_LINE_HEIGHT_PIXELS};
}

void TextRenderer::Draw(float _xPixels, float _yPixels, std::string_view _text, std::uint32_t _colorRgba, Font _font) noexcept
{
  if (m_mapped == nullptr)
  {
    return;
  }
  const auto cellWidth = static_cast<float>(MetricsOf(_font).advancePixels);
  const auto cellHeight = static_cast<float>(FONT_LINE_HEIGHT_PIXELS);

  for (std::size_t index = 0; index < _text.size(); ++index)
  {
    const auto character = static_cast<unsigned char>(_text[index]);
    std::uint32_t codepoint = character;
    if (codepoint < FONT_FIRST_CODEPOINT || codepoint >= FONT_FIRST_CODEPOINT + FONT_GLYPH_COUNT)
    {
      codepoint = REPLACEMENT_CODEPOINT;
    }
    std::uint32_t texelX = 0;
    std::uint32_t texelY = 0;
    GlyphOrigin(_font, codepoint - FONT_FIRST_CODEPOINT, texelX, texelY);
    PushQuad(_xPixels + static_cast<float>(index) * cellWidth, _yPixels, cellWidth, cellHeight, static_cast<float>(texelX),
             static_cast<float>(texelY), _colorRgba);
  }
}

void TextRenderer::DrawIcon(float _xPixels, float _yPixels, Icon _icon, std::uint32_t _colorRgba) noexcept
{
  if (m_mapped == nullptr || static_cast<std::uint32_t>(_icon) >= ICON_COUNT)
  {
    return;
  }
  const auto cell = static_cast<float>(ICON_PIXELS);
  PushQuad(_xPixels, _yPixels, cell, cell, static_cast<float>(static_cast<std::uint32_t>(_icon) * ICON_PIXELS),
           static_cast<float>(ICONS_ORIGIN_Y_TEXELS), _colorRgba);
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
  // One texel a pixel: the cell in the atlas is exactly the size of the quad on the screen.
  const float texelRight = _texelX + _widthPixels;
  const float texelBottom = _texelY + _heightPixels;

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
