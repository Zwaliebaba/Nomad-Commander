// NeuronClient/PrimitiveBatch.cpp
#include "pch.h"
#include "PrimitiveBatch.h"
#include "Debug.h"

#include <cmath>
#include <numbers>

namespace Neuron
{

namespace
{

/// Half a pixel, which is where a pixel's centre is. A line asked for at y = 5 is drawn at y = 5.5, so it covers row 5
/// rather than sitting on the boundary between rows 4 and 5 and leaving the rasterizer to choose.
constexpr float PIXEL_CENTER_OFFSET = 0.5f;

/// A quad is two triangles and therefore six vertices; there is no index buffer, because at these counts the indices
/// would cost more to upload than the duplicated corners do.
constexpr std::uint32_t VERTICES_PER_QUAD = 6;

} // namespace

PrimitiveBatch::~PrimitiveBatch()
{
  if (m_vertexBuffer != nullptr && m_mapped != nullptr)
  {
    // Unmapped with an empty written range: the CPU wrote it, and telling D3D12 "nothing" here is wrong, so the range
    // is left null to mean "the whole thing may have been written".
    m_vertexBuffer->Unmap(0, nullptr);
    m_mapped = nullptr;
  }
}

bool PrimitiveBatch::Create(GraphicsDevice& _device, PrimitiveBatch& _outBatch) noexcept
{
  NOMAD_ASSERT(_outBatch.m_vertexBuffer == nullptr);
  _outBatch.m_fault = TargetFault::None;
  _outBatch.m_result = S_OK;

  const UINT64 sliceBytes = static_cast<UINT64>(MAX_VERTICES_PER_FRAME) * sizeof(PrimitiveVertex);
  const UINT64 totalBytes = sliceBytes * FRAMES_IN_FLIGHT;

  const D3D12_HEAP_PROPERTIES heap{.Type = D3D12_HEAP_TYPE_UPLOAD,
                                   .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                                   .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
                                   .CreationNodeMask = 1,
                                   .VisibleNodeMask = 1};
  const D3D12_RESOURCE_DESC buffer{.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
                                   .Alignment = 0,
                                   .Width = totalBytes,
                                   .Height = 1,
                                   .DepthOrArraySize = 1,
                                   .MipLevels = 1,
                                   .Format = DXGI_FORMAT_UNKNOWN,
                                   .SampleDesc = {1, 0},
                                   .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                                   .Flags = D3D12_RESOURCE_FLAG_NONE};

  _outBatch.m_result = _device.Device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                                 nullptr, IID_PPV_ARGS(&_outBatch.m_vertexBuffer));
  if (FAILED(_outBatch.m_result))
  {
    _outBatch.m_fault = TargetFaultFromResult(_outBatch.m_result);
    return false;
  }

  // Mapped once and left mapped for the object's life. An upload heap is CPU-visible memory the GPU reads directly;
  // mapping and unmapping it every frame buys nothing and costs a driver round trip.
  void* mapped = nullptr;
  const D3D12_RANGE readsNothing{0, 0};
  _outBatch.m_result = _outBatch.m_vertexBuffer->Map(0, &readsNothing, &mapped);
  if (FAILED(_outBatch.m_result))
  {
    _outBatch.m_fault = TargetFaultFromResult(_outBatch.m_result);
    return false;
  }
  _outBatch.m_mapped = static_cast<PrimitiveVertex*>(mapped);
  _outBatch.m_bufferAddress = _outBatch.m_vertexBuffer->GetGPUVirtualAddress();
  return true;
}

void PrimitiveBatch::Begin(ID3D12GraphicsCommandList* _commandList, const PrimitivePipeline& _pipeline, std::uint32_t _frameSlot,
                           std::uint32_t _targetWidthPixels, std::uint32_t _targetHeightPixels) noexcept
{
  NOMAD_ASSERT(_frameSlot < FRAMES_IN_FLIGHT);
  m_commandList = _commandList;
  m_pipeline = &_pipeline;
  m_frameSlot = _frameSlot < FRAMES_IN_FLIGHT ? _frameSlot : 0;
  m_targetWidthPixels = _targetWidthPixels;
  m_targetHeightPixels = _targetHeightPixels;
  m_vertexCount = 0;
  m_overflowed = false;
  m_runCount = 0;

  if (_commandList == nullptr)
  {
    return;
  }
  const D3D12_VIEWPORT viewport{.TopLeftX = 0.0f,
                                .TopLeftY = 0.0f,
                                .Width = static_cast<FLOAT>(_targetWidthPixels),
                                .Height = static_cast<FLOAT>(_targetHeightPixels),
                                .MinDepth = 0.0f,
                                .MaxDepth = 1.0f};
  const D3D12_RECT scissor{
    .left = 0, .top = 0, .right = static_cast<LONG>(_targetWidthPixels), .bottom = static_cast<LONG>(_targetHeightPixels)};
  _commandList->RSSetViewports(1, &viewport);
  _commandList->RSSetScissorRects(1, &scissor);
}

PrimitiveVertex* PrimitiveBatch::Reserve(std::uint32_t _vertexCount, D3D12_PRIMITIVE_TOPOLOGY _topology) noexcept
{
  if (m_mapped == nullptr || m_vertexCount + _vertexCount > MAX_VERTICES_PER_FRAME)
  {
    // The primitive is dropped rather than written past the end of the slice. A frame that overflows is a defect in
    // the caller, and Overflowed() is how it finds out; corrupting the next slice would be a defect nobody could find.
    m_overflowed = true;
    return nullptr;
  }

  // A new run only when the topology changes: consecutive triangles share one draw, and a line between two rectangles
  // splits them into three runs, which is what painter's order costs and is worth.
  if (m_runCount == 0 || m_runs[m_runCount - 1].topology != _topology)
  {
    if (m_runCount == MAX_RUNS_PER_FRAME)
    {
      m_overflowed = true;
      return nullptr;
    }
    m_runs[m_runCount] = Run{_topology, m_vertexCount, 0};
    ++m_runCount;
  }
  m_runs[m_runCount - 1].vertexCount += _vertexCount;

  PrimitiveVertex* const slice = m_mapped + static_cast<std::size_t>(m_frameSlot) * MAX_VERTICES_PER_FRAME;
  PrimitiveVertex* const cursor = slice + m_vertexCount;
  m_vertexCount += _vertexCount;
  return cursor;
}

void PrimitiveBatch::FillRect(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, std::uint32_t _colorRgba) noexcept
{
  if (_widthPixels <= 0.0f || _heightPixels <= 0.0f)
  {
    return;
  }
  PrimitiveVertex* const vertices = Reserve(VERTICES_PER_QUAD, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  if (vertices == nullptr)
  {
    return;
  }
  const float left = _xPixels;
  const float top = _yPixels;
  const float right = _xPixels + _widthPixels;
  const float bottom = _yPixels + _heightPixels;
  vertices[0] = {left, top, _colorRgba};
  vertices[1] = {right, top, _colorRgba};
  vertices[2] = {left, bottom, _colorRgba};
  vertices[3] = {right, top, _colorRgba};
  vertices[4] = {right, bottom, _colorRgba};
  vertices[5] = {left, bottom, _colorRgba};
}

void PrimitiveBatch::Rect(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, std::uint32_t _colorRgba) noexcept
{
  if (_widthPixels <= 0.0f || _heightPixels <= 0.0f)
  {
    return;
  }
  FillRect(_xPixels, _yPixels, _widthPixels, 1.0f, _colorRgba);
  FillRect(_xPixels, _yPixels + _heightPixels - 1.0f, _widthPixels, 1.0f, _colorRgba);
  // The sides stop short of the rows already drawn, so no pixel is written twice. Nothing here blends, so overdraw
  // would be invisible -- but a corner drawn twice is a corner that blending would later show, and this is cheaper.
  if (_heightPixels > 2.0f)
  {
    FillRect(_xPixels, _yPixels + 1.0f, 1.0f, _heightPixels - 2.0f, _colorRgba);
    FillRect(_xPixels + _widthPixels - 1.0f, _yPixels + 1.0f, 1.0f, _heightPixels - 2.0f, _colorRgba);
  }
}

void PrimitiveBatch::Line(float _fromXPixels, float _fromYPixels, float _toXPixels, float _toYPixels, std::uint32_t _colorRgba) noexcept
{
  PrimitiveVertex* const vertices = Reserve(2, D3D_PRIMITIVE_TOPOLOGY_LINELIST);
  if (vertices == nullptr)
  {
    return;
  }
  vertices[0] = {_fromXPixels + PIXEL_CENTER_OFFSET, _fromYPixels + PIXEL_CENTER_OFFSET, _colorRgba};
  vertices[1] = {_toXPixels + PIXEL_CENTER_OFFSET, _toYPixels + PIXEL_CENTER_OFFSET, _colorRgba};
}

void PrimitiveBatch::FillPolygon(std::span<const Point> _points, std::uint32_t _colorRgba) noexcept
{
  if (_points.size() < 3)
  {
    return;
  }
  const std::uint32_t triangles = static_cast<std::uint32_t>(_points.size()) - 2;
  PrimitiveVertex* const vertices = Reserve(triangles * 3, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  if (vertices == nullptr)
  {
    return;
  }
  for (std::uint32_t triangle = 0; triangle < triangles; ++triangle)
  {
    vertices[triangle * 3 + 0] = {_points[0].xPixels, _points[0].yPixels, _colorRgba};
    vertices[triangle * 3 + 1] = {_points[triangle + 1].xPixels, _points[triangle + 1].yPixels, _colorRgba};
    vertices[triangle * 3 + 2] = {_points[triangle + 2].xPixels, _points[triangle + 2].yPixels, _colorRgba};
  }
}

void PrimitiveBatch::FillCircle(Point _center, float _radiusPixels, std::uint32_t _segments, std::uint32_t _colorRgba) noexcept
{
  if (_radiusPixels <= 0.0f || _segments < 3)
  {
    return;
  }
  PrimitiveVertex* const vertices = Reserve(_segments * 3, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  if (vertices == nullptr)
  {
    return;
  }
  const float step = 2.0f * std::numbers::pi_v<float> / static_cast<float>(_segments);
  for (std::uint32_t segment = 0; segment < _segments; ++segment)
  {
    const float from = step * static_cast<float>(segment);
    const float to = step * static_cast<float>(segment + 1);
    vertices[segment * 3 + 0] = {_center.xPixels, _center.yPixels, _colorRgba};
    vertices[segment * 3 + 1] = {_center.xPixels + _radiusPixels * std::cos(from), _center.yPixels + _radiusPixels * std::sin(from),
                                 _colorRgba};
    vertices[segment * 3 + 2] = {_center.xPixels + _radiusPixels * std::cos(to), _center.yPixels + _radiusPixels * std::sin(to),
                                 _colorRgba};
  }
}

void PrimitiveBatch::End() noexcept
{
  if (m_commandList == nullptr || m_pipeline == nullptr || m_vertexCount == 0)
  {
    m_commandList = nullptr;
    m_pipeline = nullptr;
    return;
  }

  D3D12_VERTEX_BUFFER_VIEW view{};
  view.BufferLocation = m_bufferAddress + static_cast<UINT64>(m_frameSlot) * MAX_VERTICES_PER_FRAME * sizeof(PrimitiveVertex);
  view.SizeInBytes = m_vertexCount * sizeof(PrimitiveVertex);
  view.StrideInBytes = sizeof(PrimitiveVertex);
  m_commandList->IASetVertexBuffers(0, 1, &view);
  m_commandList->SetGraphicsRootSignature(m_pipeline->RootSignature());
  const std::uint32_t screenSize[PrimitivePipeline::ROOT_CONSTANT_COUNT] = {m_targetWidthPixels, m_targetHeightPixels};
  m_commandList->SetGraphicsRoot32BitConstants(0, PrimitivePipeline::ROOT_CONSTANT_COUNT, screenSize, 0);

  // One draw per run, in the order the runs were made, which is the order the caller asked for. The pipeline state
  // changes with the topology because the two differ in exactly that.
  D3D12_PRIMITIVE_TOPOLOGY bound = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
  for (std::uint32_t index = 0; index < m_runCount; ++index)
  {
    const Run& run = m_runs[index];
    if (run.topology != bound)
    {
      m_commandList->SetPipelineState(run.topology == D3D_PRIMITIVE_TOPOLOGY_LINELIST ? m_pipeline->LinePipeline()
                                                                                      : m_pipeline->TrianglePipeline());
      m_commandList->IASetPrimitiveTopology(run.topology);
      bound = run.topology;
    }
    m_commandList->DrawInstanced(run.vertexCount, 1, run.firstVertex, 0);
  }
  m_commandList = nullptr;
  m_pipeline = nullptr;
}

} // namespace Neuron
