// NeuronClient/MeshBuilder.h
#pragma once

#include "GraphicsDevice.h"
#include "NeuronCore.h"
#include "SceneTarget.h"

#include <DirectXMath.h>
#include <cstdint>
#include <d3d12.h>
#include <vector>
#include <wrl/client.h>

namespace Neuron
{

/// One vertex of the map's geometry: a position in world units and a packed colour. Plain fields (R8); `float`
/// because this is the client, where R16's integer rule does not bind.
struct MeshVertex
{
  float xUnits;
  float yUnits;
  float zUnits;
  std::uint32_t colorRgba;
};

/// A stretch of the index buffer: one shape, so a caller can draw the sphere without drawing the lanes.
struct MeshSection
{
  std::uint32_t firstIndex;
  std::uint32_t indexCount;
};

/// The two shapes the map needs, generated at runtime and never loaded (R13), and the GPU buffers they live in.
///
/// **Everything is built and uploaded once.** Shapes are appended on the CPU, `Upload` makes the buffers, and from
/// then on a frame only binds and draws — NC-027's criterion that the frame loop allocates nothing.
///
/// There is no scene graph, no material, no model format and no loader. A shape is vertices and indices, and that is
/// the whole of what this type knows.
class MeshBuilder
{
public:
  /// A sphere of this many stacks and slices has this many vertices and indices. `constexpr` so a caller can size a
  /// buffer, or a test can assert the count without building one.
  [[nodiscard]] static constexpr std::uint32_t SphereVertexCount(std::uint32_t _stacks, std::uint32_t _slices) noexcept
  {
    // Rings from pole to pole, each with one repeated vertex to close the seam.
    return (_stacks + 1) * (_slices + 1);
  }

  [[nodiscard]] static constexpr std::uint32_t SphereIndexCount(std::uint32_t _stacks, std::uint32_t _slices) noexcept
  {
    return _stacks * _slices * 6;
  }

  /// A lane is one quad: four vertices, six indices.
  static constexpr std::uint32_t LANE_VERTEX_COUNT = 4;
  static constexpr std::uint32_t LANE_INDEX_COUNT = 6;

  MeshBuilder() = default;
  MeshBuilder(const MeshBuilder&) = delete;
  MeshBuilder& operator=(const MeshBuilder&) = delete;
  MeshBuilder(MeshBuilder&&) = delete;
  MeshBuilder& operator=(MeshBuilder&&) = delete;
  ~MeshBuilder();

  /// A UV sphere at `_center`, shaded from `_highlightRgba` at the top through `_bodyRgba` to `_limbRgba` at the
  /// bottom. That is the treatment UI §2 already describes for the 2D map's discs, and it is why a sphere reads as
  /// round here with no light in the scene.
  MeshSection AddSphere(DirectX::XMFLOAT3 _centerUnits, float _radiusUnits, std::uint32_t _stacks, std::uint32_t _slices,
                        std::uint32_t _highlightRgba, std::uint32_t _bodyRgba, std::uint32_t _limbRgba);

  /// A lane between two points, as a flat quad of the given width lying in the map's plane.
  MeshSection AddLane(DirectX::XMFLOAT3 _fromUnits, DirectX::XMFLOAT3 _toUnits, float _widthUnits, std::uint32_t _colorRgba);

  /// Creates the vertex and index buffers from everything appended so far. Called once.
  [[nodiscard]] bool Upload(GraphicsDevice& _device) noexcept;

  /// Binds the buffers. The caller has already set the pipeline and the root constants.
  void Bind(ID3D12GraphicsCommandList* _commandList) const noexcept;

  /// Draws one shape.
  void Draw(ID3D12GraphicsCommandList* _commandList, const MeshSection& _section) const noexcept;

  [[nodiscard]] std::uint32_t VertexCount() const noexcept
  {
    return static_cast<std::uint32_t>(m_vertices.size());
  }

  [[nodiscard]] std::uint32_t IndexCount() const noexcept
  {
    return static_cast<std::uint32_t>(m_indices.size());
  }

  /// The geometry as built, before upload — what the tests inspect.
  [[nodiscard]] const std::vector<MeshVertex>& Vertices() const noexcept
  {
    return m_vertices;
  }

  [[nodiscard]] const std::vector<std::uint32_t>& Indices() const noexcept
  {
    return m_indices;
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
  std::vector<MeshVertex> m_vertices;
  std::vector<std::uint32_t> m_indices;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_vertexBuffer;
  Microsoft::WRL::ComPtr<ID3D12Resource> m_indexBuffer;
  D3D12_VERTEX_BUFFER_VIEW m_vertexView{};
  D3D12_INDEX_BUFFER_VIEW m_indexView{};
  void* m_mappedVertices = nullptr;
  void* m_mappedIndices = nullptr;
  HRESULT m_result = S_OK;
  TargetFault m_fault = TargetFault::None;
};

} // namespace Neuron
