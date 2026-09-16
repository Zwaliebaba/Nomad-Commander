// NeuronClient/PrimitiveBatch.h
#pragma once

#include "GraphicsDevice.h"
#include "NeuronCore.h"
#include "PrimitivePipeline.h"
#include "SceneTarget.h"
#include "SwapChainTarget.h"

#include <cstdint>
#include <d3d12.h>
#include <span>
#include <wrl/client.h>

namespace Neuron
{

/// One vertex of a 2D primitive: a position in pixels and a packed colour. A public aggregate, so plain fields (R8),
/// and `float` because this is the client -- R16's integer rule binds GameLogic, where a replay depends on the sum.
struct PrimitiveVertex
{
  float positionXPixels;
  float positionYPixels;
  /// Red in the low byte, alpha in the high one: 0xFF0000FF is opaque red. The same order the scene target reads back
  /// in, so a colour authored here and a pixel asserted in a test are the same number.
  std::uint32_t colorRgba;
};

/// A point in the same pixel space, for the polygon and circle calls.
struct Point
{
  float xPixels;
  float yPixels;
};

/// Everything the map and the desk draw that is not text: filled rectangles, outlines, lines, filled convex polygons
/// and circles, in pixel coordinates with the origin at the top left, in painter's order.
///
/// **Painter's order is the contract**, and it is why this does not sort by pipeline: the draws are recorded in the
/// order they were asked for, and a run of triangles followed by a run of lines followed by more triangles issues
/// three draws. Sorting would be faster and would put a line under a rectangle that was drawn before it.
///
/// Vertices go into a persistently mapped upload buffer with one slice per frame in flight, indexed by the swap
/// chain's back-buffer slot. NC-021 already waits on that slot's fence before a frame opens, so the slice being
/// written is never one the GPU is still reading.
class PrimitiveBatch
{
public:
  /// One slice per back buffer. It is the swap chain's count because the slice is chosen by its buffer index.
  static constexpr std::uint32_t FRAMES_IN_FLIGHT = SwapChainTarget::BUFFER_COUNT;

  /// Vertices per slice. 50,000 is what the task asks a frame to survive; this is comfortably above it, and at twelve
  /// bytes a vertex the whole ring is about three megabytes.
  static constexpr std::uint32_t MAX_VERTICES_PER_FRAME = 131072;

  /// Topology switches a frame may make. A run is a stretch of vertices drawn with one topology, so this is how
  /// many times a frame may alternate between triangles and lines; the desk alternates a few hundred times at most.
  /// It is a fixed array rather than a vector because Reserve runs once per primitive and must not allocate: an
  /// allocation inside a noexcept function is std::terminate on a machine low on memory.
  static constexpr std::uint32_t MAX_RUNS_PER_FRAME = 4096;

  /// What a circle gets when the caller does not say. The map's nodes are small; a circle wider than this many pixels
  /// should ask for more.
  static constexpr std::uint32_t DEFAULT_CIRCLE_SEGMENTS = 24;

  PrimitiveBatch() = default;
  PrimitiveBatch(const PrimitiveBatch&) = delete;
  PrimitiveBatch& operator=(const PrimitiveBatch&) = delete;
  PrimitiveBatch(PrimitiveBatch&&) = delete;
  PrimitiveBatch& operator=(PrimitiveBatch&&) = delete;
  ~PrimitiveBatch();

  [[nodiscard]] static bool Create(GraphicsDevice& _device, PrimitiveBatch& _outBatch) noexcept;

  /// Opens a batch against the frame's command list and the slice belonging to that frame's back buffer.
  ///
  /// The target size is what the vertex shader turns pixels into clip space with, and it is also what the viewport and
  /// scissor are set to — one number, given once, so a caller cannot set a viewport that disagrees with the shader.
  /// Binding the render target itself stays the caller's, because the batch does not own it.
  void Begin(ID3D12GraphicsCommandList* _commandList, const PrimitivePipeline& _pipeline, std::uint32_t _frameSlot,
             std::uint32_t _targetWidthPixels, std::uint32_t _targetHeightPixels) noexcept;

  /// The rectangle covering pixels [x, x + width) by [y, y + height). A pixel coordinate names a pixel's top-left
  /// corner, so FillRect(20, 30, 10, 10) fills exactly pixels 20..29 by 30..39 and nothing else.
  void FillRect(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, std::uint32_t _colorRgba) noexcept;

  /// A one-pixel outline just inside that rectangle, drawn as four filled rectangles rather than four lines. Line
  /// rasterization follows the diamond-exit rule, which makes the pixels a line ends on a matter of argument; four
  /// filled rectangles are exact, and an outline is something a UI aligns other things to.
  void Rect(float _xPixels, float _yPixels, float _widthPixels, float _heightPixels, std::uint32_t _colorRgba) noexcept;

  /// A one-pixel line. The coordinates are shifted to pixel centres, so Line(10, 5, 20, 5) covers row 5 from column 10
  /// to column 19 rather than landing on the boundary between two rows.
  void Line(float _fromXPixels, float _fromYPixels, float _toXPixels, float _toYPixels, std::uint32_t _colorRgba) noexcept;

  /// A filled convex polygon, as a fan from its first point. Convex because a fan is only correct for convex shapes,
  /// and nothing this game draws is not.
  void FillPolygon(std::span<const Point> _points, std::uint32_t _colorRgba) noexcept;

  /// A filled circle, which is a polygon with enough sides not to look like one.
  void FillCircle(Point _center, float _radiusPixels, std::uint32_t _segments, std::uint32_t _colorRgba) noexcept;

  /// Records the draws, in the order they were asked for, and closes the batch.
  void End() noexcept;

  /// How many vertices the open batch has taken. The frame that overflows a slice is the one worth knowing about.
  [[nodiscard]] std::uint32_t VertexCount() const noexcept
  {
    return m_vertexCount;
  }

  /// True once a call has been dropped because the slice was full, until the next Begin.
  [[nodiscard]] bool Overflowed() const noexcept
  {
    return m_overflowed;
  }

  [[nodiscard]] TargetFault Fault() const noexcept
  {
    return m_fault;
  }

  [[nodiscard]] HRESULT Result() const noexcept
  {
    return m_result;
  }

private:
  /// One contiguous stretch of vertices drawn with one topology. A new run starts whenever the topology changes,
  /// which is what keeps painter's order.
  struct Run
  {
    D3D12_PRIMITIVE_TOPOLOGY topology;
    std::uint32_t firstVertex;
    std::uint32_t vertexCount;
  };

  /// Reserves room for that many vertices in the open slice and returns where to write them, or nullptr when the
  /// slice is full. A full slice drops the primitive and records it; it never writes past the end.
  [[nodiscard]] PrimitiveVertex* Reserve(std::uint32_t _vertexCount, D3D12_PRIMITIVE_TOPOLOGY _topology) noexcept;

  Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
  PrimitiveVertex* m_mapped = nullptr;
  ID3D12GraphicsCommandList* m_commandList = nullptr;
  const PrimitivePipeline* m_pipeline = nullptr;
  Run m_runs[MAX_RUNS_PER_FRAME]{};
  D3D12_GPU_VIRTUAL_ADDRESS m_bufferAddress = 0;
  std::uint32_t m_frameSlot = 0;
  std::uint32_t m_vertexCount = 0;
  std::uint32_t m_runCount = 0;
  std::uint32_t m_targetWidthPixels = 0;
  std::uint32_t m_targetHeightPixels = 0;
  HRESULT m_result = S_OK;
  TargetFault m_fault = TargetFault::None;
  bool m_overflowed = false;
};

} // namespace Neuron
