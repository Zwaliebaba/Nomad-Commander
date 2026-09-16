// NeuronClient/MeshBuilder.cpp
#include "pch.h"
#include "MeshBuilder.h"
#include "Debug.h"

#include <cmath>
#include <cstring>
#include <numbers>

namespace Neuron
{

namespace
{

/// Blends two packed colours. Used to shade a sphere from highlight through body to limb without a light in the
/// scene: the same treatment UI §2 gives the 2D map's discs, done with vertices instead of concentric circles.
[[nodiscard]] std::uint32_t Mix(std::uint32_t _from, std::uint32_t _to, float _amount) noexcept
{
  const float amount = _amount < 0.0f ? 0.0f : (_amount > 1.0f ? 1.0f : _amount);
  std::uint32_t result = 0;
  for (int shift = 0; shift < 32; shift += 8)
  {
    const auto left = static_cast<float>((_from >> shift) & 0xFFu);
    const auto right = static_cast<float>((_to >> shift) & 0xFFu);
    // lround rather than a cast of (x + 0.5): the cast truncates toward zero and rounds a negative wrongly,
    // which this cannot produce today and would the moment somebody blends with a signed intermediate.
    const auto blended = static_cast<std::uint32_t>(std::lround(left + (right - left) * amount));
    result |= (blended & 0xFFu) << shift;
  }
  return result;
}

[[nodiscard]] D3D12_HEAP_PROPERTIES UploadHeap() noexcept
{
  return D3D12_HEAP_PROPERTIES{.Type = D3D12_HEAP_TYPE_UPLOAD,
                               .CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
                               .MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN,
                               .CreationNodeMask = 1,
                               .VisibleNodeMask = 1};
}

[[nodiscard]] D3D12_RESOURCE_DESC BufferDesc(UINT64 _bytes) noexcept
{
  return D3D12_RESOURCE_DESC{.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER,
                             .Alignment = 0,
                             .Width = _bytes,
                             .Height = 1,
                             .DepthOrArraySize = 1,
                             .MipLevels = 1,
                             .Format = DXGI_FORMAT_UNKNOWN,
                             .SampleDesc = {1, 0},
                             .Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR,
                             .Flags = D3D12_RESOURCE_FLAG_NONE};
}

} // namespace

MeshBuilder::~MeshBuilder()
{
  if (m_vertexBuffer != nullptr && m_mappedVertices != nullptr)
  {
    m_vertexBuffer->Unmap(0, nullptr);
    m_mappedVertices = nullptr;
  }
  if (m_indexBuffer != nullptr && m_mappedIndices != nullptr)
  {
    m_indexBuffer->Unmap(0, nullptr);
    m_mappedIndices = nullptr;
  }
}

MeshSection MeshBuilder::AddSphere(DirectX::XMFLOAT3 _centerUnits, float _radiusUnits, std::uint32_t _stacks, std::uint32_t _slices,
                                   std::uint32_t _highlightRgba, std::uint32_t _bodyRgba, std::uint32_t _limbRgba)
{
  const MeshSection section{static_cast<std::uint32_t>(m_indices.size()), SphereIndexCount(_stacks, _slices)};
  if (_stacks == 0 || _slices == 0 || _radiusUnits <= 0.0f)
  {
    return MeshSection{section.firstIndex, 0};
  }
  const auto baseVertex = static_cast<std::uint32_t>(m_vertices.size());

  // A UV sphere: rings of vertices from the north pole to the south, each ring closed by repeating its first vertex
  // so the seam has its own vertices rather than wrapping a texture coordinate nobody has.
  for (std::uint32_t stack = 0; stack <= _stacks; ++stack)
  {
    const float latitude = std::numbers::pi_v<float> * static_cast<float>(stack) / static_cast<float>(_stacks);
    const float ringRadius = std::sin(latitude);
    const float height = std::cos(latitude);
    // The shade runs highlight -> body over the top half and body -> limb over the bottom, which puts the bright
    // side up. No light: this is a colour ramp along one axis, and it is what makes the sphere read as round.
    const float downward = static_cast<float>(stack) / static_cast<float>(_stacks);
    const std::uint32_t shade =
      downward < 0.5f ? Mix(_highlightRgba, _bodyRgba, downward * 2.0f) : Mix(_bodyRgba, _limbRgba, (downward - 0.5f) * 2.0f);

    for (std::uint32_t slice = 0; slice <= _slices; ++slice)
    {
      const float longitude = 2.0f * std::numbers::pi_v<float> * static_cast<float>(slice) / static_cast<float>(_slices);
      m_vertices.push_back(MeshVertex{_centerUnits.x + _radiusUnits * ringRadius * std::cos(longitude),
                                      _centerUnits.y + _radiusUnits * height,
                                      _centerUnits.z + _radiusUnits * ringRadius * std::sin(longitude), shade});
    }
  }

  for (std::uint32_t stack = 0; stack < _stacks; ++stack)
  {
    for (std::uint32_t slice = 0; slice < _slices; ++slice)
    {
      const std::uint32_t topLeft = baseVertex + stack * (_slices + 1) + slice;
      const std::uint32_t bottomLeft = topLeft + _slices + 1;
      m_indices.push_back(topLeft);
      m_indices.push_back(bottomLeft);
      m_indices.push_back(topLeft + 1);
      m_indices.push_back(topLeft + 1);
      m_indices.push_back(bottomLeft);
      m_indices.push_back(bottomLeft + 1);
    }
  }
  return section;
}

MeshSection MeshBuilder::AddLane(DirectX::XMFLOAT3 _fromUnits, DirectX::XMFLOAT3 _toUnits, float _widthUnits, std::uint32_t _colorRgba)
{
  const MeshSection section{static_cast<std::uint32_t>(m_indices.size()), LANE_INDEX_COUNT};
  const auto baseVertex = static_cast<std::uint32_t>(m_vertices.size());

  // A flat quad lying in the map's plane: the lane's direction, and a perpendicular in the same plane. The map is
  // drawn on the xz plane, so "up" out of it is y and the width runs along the cross product.
  const float deltaX = _toUnits.x - _fromUnits.x;
  const float deltaZ = _toUnits.z - _fromUnits.z;
  const float length = std::sqrt(deltaX * deltaX + deltaZ * deltaZ);
  if (length <= 0.0f || _widthUnits <= 0.0f)
  {
    return MeshSection{section.firstIndex, 0};
  }
  const float halfWidth = _widthUnits * 0.5f;
  const float sideX = -deltaZ / length * halfWidth;
  const float sideZ = deltaX / length * halfWidth;

  m_vertices.push_back(MeshVertex{_fromUnits.x + sideX, _fromUnits.y, _fromUnits.z + sideZ, _colorRgba});
  m_vertices.push_back(MeshVertex{_fromUnits.x - sideX, _fromUnits.y, _fromUnits.z - sideZ, _colorRgba});
  m_vertices.push_back(MeshVertex{_toUnits.x + sideX, _toUnits.y, _toUnits.z + sideZ, _colorRgba});
  m_vertices.push_back(MeshVertex{_toUnits.x - sideX, _toUnits.y, _toUnits.z - sideZ, _colorRgba});

  m_indices.push_back(baseVertex + 0);
  m_indices.push_back(baseVertex + 2);
  m_indices.push_back(baseVertex + 1);
  m_indices.push_back(baseVertex + 1);
  m_indices.push_back(baseVertex + 2);
  m_indices.push_back(baseVertex + 3);
  return section;
}

bool MeshBuilder::Upload(GraphicsDevice& _device) noexcept
{
  NOMAD_ASSERT(m_vertexBuffer == nullptr);
  m_fault = TargetFault::None;
  m_result = S_OK;
  if (m_vertices.empty() || m_indices.empty())
  {
    m_fault = TargetFault::Allocation;
    return false;
  }

  const UINT64 vertexBytes = static_cast<UINT64>(m_vertices.size()) * sizeof(MeshVertex);
  const UINT64 indexBytes = static_cast<UINT64>(m_indices.size()) * sizeof(std::uint32_t);
  const D3D12_HEAP_PROPERTIES heap = UploadHeap();
  const D3D12_RESOURCE_DESC vertexDesc = BufferDesc(vertexBytes);
  const D3D12_RESOURCE_DESC indexDesc = BufferDesc(indexBytes);
  const D3D12_RANGE readsNothing{0, 0};

  // An upload heap rather than a default heap with a copy: this geometry is written once and read every frame, and a
  // few thousand vertices out of CPU-visible memory costs less than the copy queue would to set up. If the map ever
  // carries a hundred thousand, that is the moment to move it and this comment is the note to whoever does.
  m_result = _device.Device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &vertexDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                       IID_PPV_ARGS(&m_vertexBuffer));
  if (FAILED(m_result))
  {
    m_fault = TargetFaultFromResult(m_result);
    return false;
  }
  m_result = _device.Device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &indexDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                       IID_PPV_ARGS(&m_indexBuffer));
  if (FAILED(m_result))
  {
    m_fault = TargetFaultFromResult(m_result);
    return false;
  }

  m_result = m_vertexBuffer->Map(0, &readsNothing, &m_mappedVertices);
  if (FAILED(m_result))
  {
    m_fault = TargetFaultFromResult(m_result);
    return false;
  }
  std::memcpy(m_mappedVertices, m_vertices.data(), static_cast<std::size_t>(vertexBytes));

  m_result = m_indexBuffer->Map(0, &readsNothing, &m_mappedIndices);
  if (FAILED(m_result))
  {
    m_fault = TargetFaultFromResult(m_result);
    return false;
  }
  std::memcpy(m_mappedIndices, m_indices.data(), static_cast<std::size_t>(indexBytes));

  m_vertexView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
  m_vertexView.SizeInBytes = static_cast<UINT>(vertexBytes);
  m_vertexView.StrideInBytes = sizeof(MeshVertex);
  m_indexView.BufferLocation = m_indexBuffer->GetGPUVirtualAddress();
  m_indexView.SizeInBytes = static_cast<UINT>(indexBytes);
  m_indexView.Format = DXGI_FORMAT_R32_UINT;
  return true;
}

void MeshBuilder::Bind(ID3D12GraphicsCommandList* _commandList) const noexcept
{
  if (_commandList == nullptr || m_vertexBuffer == nullptr)
  {
    return;
  }
  _commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  _commandList->IASetVertexBuffers(0, 1, &m_vertexView);
  _commandList->IASetIndexBuffer(&m_indexView);
}

void MeshBuilder::Draw(ID3D12GraphicsCommandList* _commandList, const MeshSection& _section) const noexcept
{
  if (_commandList == nullptr || m_vertexBuffer == nullptr || _section.indexCount == 0)
  {
    return;
  }
  _commandList->DrawIndexedInstanced(_section.indexCount, 1, _section.firstIndex, 0, 0);
}

} // namespace Neuron
