// Tests/NeuronClientTests/MeshPipelineTests.cpp
#include "pch.h"
#include "Camera.h"
#include "DepthTarget.h"
#include "GraphicsDevice.h"
#include "MeshBuilder.h"
#include "MeshPipeline.h"
#include "SceneTarget.h"
#include "Window.h"
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

using Microsoft::WRL::ComPtr;

constexpr std::uint32_t BACKGROUND = 0xFF000000u;
constexpr std::uint32_t NEAR_COLOR = 0xFF0000FFu; ///< opaque red, the sphere in front
constexpr std::uint32_t FAR_COLOR = 0xFF00FF00u;  ///< opaque green, the sphere behind

[[nodiscard]] std::size_t PixelIndex(std::uint32_t _x, std::uint32_t _y)
{
  return static_cast<std::size_t>(_y) * Neuron::SCREEN_WIDTH_PIXELS + _x;
}

} // namespace

TEST_CLASS(CameraTests)
{
public:
  TEST_METHOD(TheViewProjectionPutsTheTargetAtTheCenterOfTheScreen)
  {
    // Hand-computed rather than compared against another matrix library: a camera looking at a point must project
    // that point to the middle of clip space, whatever else it does.
    Neuron::Camera camera;
    camera.positionUnits = {0.0f, 0.0f, -10.0f};
    camera.targetUnits = {0.0f, 0.0f, 0.0f};
    camera.aspectRatio = 16.0f / 9.0f;

    const DirectX::XMFLOAT4X4 stored = camera.ViewProjection();
    // ViewProjection transposes on the way out for HLSL, so transpose back to multiply row-vector style here.
    const DirectX::XMMATRIX matrix = DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&stored));
    const DirectX::XMVECTOR projected = DirectX::XMVector4Transform(DirectX::XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f), matrix);
    const float w = DirectX::XMVectorGetW(projected);
    Assert::IsTrue(w > 0.0f, L"the target must be in front of the camera");
    Assert::IsTrue(std::fabs(DirectX::XMVectorGetX(projected) / w) < 0.0001f, L"the target must be centered in x");
    Assert::IsTrue(std::fabs(DirectX::XMVectorGetY(projected) / w) < 0.0001f, L"the target must be centered in y");
  }

  TEST_METHOD(ANearerPointHasASmallerDepth)
  {
    // The whole reason there is a depth buffer: nearer must compare less, because the pipeline tests LESS.
    Neuron::Camera camera;
    camera.positionUnits = {0.0f, 0.0f, -10.0f};
    camera.targetUnits = {0.0f, 0.0f, 0.0f};

    const DirectX::XMFLOAT4X4 stored = camera.ViewProjection();
    const DirectX::XMMATRIX matrix = DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&stored));
    const auto depthOf = [&matrix](float _z)
    {
      const DirectX::XMVECTOR point = DirectX::XMVector4Transform(DirectX::XMVectorSet(0.0f, 0.0f, _z, 1.0f), matrix);
      return DirectX::XMVectorGetZ(point) / DirectX::XMVectorGetW(point);
    };
    Assert::IsTrue(depthOf(-5.0f) < depthOf(5.0f), L"a nearer point must have a smaller depth");
    Assert::IsTrue(depthOf(-5.0f) >= 0.0f && depthOf(5.0f) <= 1.0f, L"depth must land in [0, 1]");
  }

  TEST_METHOD(AWiderFieldOfViewFitsMoreIn)
  {
    Neuron::Camera narrow;
    narrow.verticalFieldOfViewRadians = DirectX::XM_PIDIV4 / 2.0f;
    Neuron::Camera wide;
    wide.verticalFieldOfViewRadians = DirectX::XM_PIDIV4;

    const auto projectedY = [](const Neuron::Camera& _camera)
    {
      const DirectX::XMFLOAT4X4 stored = _camera.ViewProjection();
      const DirectX::XMMATRIX matrix = DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&stored));
      const DirectX::XMVECTOR point = DirectX::XMVector4Transform(DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 1.0f), matrix);
      return DirectX::XMVectorGetY(point) / DirectX::XMVectorGetW(point);
    };
    Assert::IsTrue(projectedY(wide) < projectedY(narrow), L"the same point must sit closer to the center through a wider lens");
  }
};

TEST_CLASS(MeshBuilderTests)
{
public:
  TEST_METHOD(ASpheresCountsMatchItsConstexprPromise)
  {
    // The counts are constexpr so a caller can size a buffer; if they disagreed with what AddSphere produces, that
    // caller would be wrong by however much.
    Neuron::MeshBuilder builder;
    constexpr std::uint32_t STACKS = 12;
    constexpr std::uint32_t SLICES = 16;
    const Neuron::MeshSection section = builder.AddSphere({0.0f, 0.0f, 0.0f}, 1.0f, STACKS, SLICES, 0xFFFFFFFFu, 0xFF808080u, 0xFF202020u);
    Assert::AreEqual(Neuron::MeshBuilder::SphereVertexCount(STACKS, SLICES), builder.VertexCount());
    Assert::AreEqual(Neuron::MeshBuilder::SphereIndexCount(STACKS, SLICES), builder.IndexCount());
    Assert::AreEqual(Neuron::MeshBuilder::SphereIndexCount(STACKS, SLICES), section.indexCount);
    Assert::AreEqual(0u, section.firstIndex);
  }

  TEST_METHOD(EveryIndexIsInRange)
  {
    // An index past the end is a GPU fault, or worse, silence and garbage. Cheap to rule out here.
    Neuron::MeshBuilder builder;
    (void)builder.AddSphere({0.0f, 0.0f, 0.0f}, 1.0f, 8, 10, 0xFFFFFFFFu, 0xFF808080u, 0xFF202020u);
    (void)builder.AddLane({-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 0.2f, 0xFF404040u);
    (void)builder.AddSphere({3.0f, 0.0f, 0.0f}, 0.5f, 6, 8, 0xFFFFFFFFu, 0xFF808080u, 0xFF202020u);

    const std::uint32_t vertices = builder.VertexCount();
    for (const std::uint32_t index : builder.Indices())
    {
      Assert::IsTrue(index < vertices,
                     (L"index " + std::to_wstring(index) + L" is past the " + std::to_wstring(vertices) + L" vertices there are").c_str());
    }
  }

  TEST_METHOD(EverySphereVertexIsOnItsSphere)
  {
    // The shape's own definition: every vertex is exactly the radius from the centre. It catches a stack or slice
    // loop that ran one too far as surely as looking at it would, and does not need a screen.
    Neuron::MeshBuilder builder;
    const DirectX::XMFLOAT3 center{2.0f, -1.0f, 3.0f};
    constexpr float RADIUS = 1.75f;
    (void)builder.AddSphere(center, RADIUS, 10, 14, 0xFFFFFFFFu, 0xFF808080u, 0xFF202020u);

    for (const Neuron::MeshVertex& vertex : builder.Vertices())
    {
      const float dx = vertex.xUnits - center.x;
      const float dy = vertex.yUnits - center.y;
      const float dz = vertex.zUnits - center.z;
      const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
      Assert::IsTrue(std::fabs(distance - RADIUS) < 0.001f, L"a vertex is not on the sphere it belongs to");
    }
  }

  TEST_METHOD(ALaneIsAQuadOfTheWidthItWasAskedFor)
  {
    Neuron::MeshBuilder builder;
    const Neuron::MeshSection lane = builder.AddLane({0.0f, 0.0f, 0.0f}, {10.0f, 0.0f, 0.0f}, 2.0f, 0xFF404040u);
    Assert::AreEqual(Neuron::MeshBuilder::LANE_VERTEX_COUNT, builder.VertexCount());
    Assert::AreEqual(Neuron::MeshBuilder::LANE_INDEX_COUNT, lane.indexCount);

    // Running along x, the width must appear in z, two apart.
    const std::vector<Neuron::MeshVertex>& vertices = builder.Vertices();
    Assert::IsTrue(std::fabs((vertices[0].zUnits - vertices[1].zUnits) - 2.0f) < 0.001f ||
                     std::fabs((vertices[1].zUnits - vertices[0].zUnits) - 2.0f) < 0.001f,
                   L"the lane is not the width it was asked for");
  }

  TEST_METHOD(ADegenerateShapeAddsNothing)
  {
    Neuron::MeshBuilder builder;
    const Neuron::MeshSection none = builder.AddLane({1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f}, 0.5f, 0xFFFFFFFFu);
    Assert::AreEqual(0u, none.indexCount, L"a lane of no length must add no geometry");
    Assert::AreEqual(0u, builder.IndexCount());

    const Neuron::MeshSection nothing = builder.AddSphere({0.0f, 0.0f, 0.0f}, 0.0f, 8, 8, 0u, 0u, 0u);
    Assert::AreEqual(0u, nothing.indexCount, L"a sphere of no radius must add no geometry");
  }
};

TEST_CLASS(MeshPipelineTests)
{
public:
  TEST_METHOD(TheDepthTestRejectsWhatIsBehind)
  {
    // A12 on WARP: two spheres on the same screen position, the far one drawn SECOND. Without a working depth buffer
    // the last draw wins and the pixel is green; with one, the near sphere survives and it is red.
    Neuron::GraphicsDevice device;
    const Neuron::GraphicsDevice::Desc deviceDesc{true, true};
    Assert::IsTrue(Neuron::GraphicsDevice::Create(deviceDesc, device), L"the WARP device could not be created");

    Neuron::SceneTarget scene;
    const Neuron::SceneTarget::Desc sceneDesc{
      Neuron::SCREEN_WIDTH_PIXELS, Neuron::SCREEN_HEIGHT_PIXELS, DXGI_FORMAT_R8G8B8A8_UNORM, {0.0f, 0.0f, 0.0f, 1.0f}};
    Assert::IsTrue(Neuron::SceneTarget::Create(device, sceneDesc, scene), L"the scene target could not be created");

    Neuron::DepthTarget depth;
    Assert::IsTrue(Neuron::DepthTarget::Create(device, Neuron::SCREEN_WIDTH_PIXELS, Neuron::SCREEN_HEIGHT_PIXELS, depth),
                   L"the depth buffer could not be created");

    Neuron::MeshPipeline pipeline;
    Assert::IsTrue(Neuron::MeshPipeline::Create(device, pipeline), L"the mesh pipeline could not be created");

    Neuron::MeshBuilder builder;
    // The near one is red, the far one green, both on the camera's axis so they overlap at the center of the screen.
    // Not named "near" and "far": windef.h still #defines both, a leftover of 16-bit Windows.
    const Neuron::MeshSection nearSphere = builder.AddSphere({0.0f, 0.0f, 0.0f}, 2.0f, 16, 24, NEAR_COLOR, NEAR_COLOR, NEAR_COLOR);
    const Neuron::MeshSection farSphere = builder.AddSphere({0.0f, 0.0f, 6.0f}, 2.0f, 16, 24, FAR_COLOR, FAR_COLOR, FAR_COLOR);
    Assert::IsTrue(builder.Upload(device), L"the geometry could not be uploaded");

    Neuron::Camera camera;
    camera.positionUnits = {0.0f, 0.0f, -10.0f};
    camera.targetUnits = {0.0f, 0.0f, 0.0f};
    camera.aspectRatio = static_cast<float>(Neuron::SCREEN_WIDTH_PIXELS) / static_cast<float>(Neuron::SCREEN_HEIGHT_PIXELS);

    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;
    Assert::IsTrue(SUCCEEDED(device.Device()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))));
    Assert::IsTrue(SUCCEEDED(
      device.Device()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commandList))));

    scene.Transition(commandList.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
    scene.Clear(commandList.Get());
    depth.Clear(commandList.Get());
    const D3D12_CPU_DESCRIPTOR_HANDLE sceneView = scene.RenderTargetView();
    const D3D12_CPU_DESCRIPTOR_HANDLE depthView = depth.DepthStencilView();
    commandList->OMSetRenderTargets(1, &sceneView, FALSE, &depthView);
    const D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<FLOAT>(scene.WidthPixels()), static_cast<FLOAT>(scene.HeightPixels()),
                                  0.0f, 1.0f};
    const D3D12_RECT scissor{0, 0, static_cast<LONG>(scene.WidthPixels()), static_cast<LONG>(scene.HeightPixels())};
    commandList->RSSetViewports(1, &viewport);
    commandList->RSSetScissorRects(1, &scissor);

    pipeline.Begin(commandList.Get(), camera);
    builder.Bind(commandList.Get());
    builder.Draw(commandList.Get(), nearSphere);
    // Drawn second and further away: the depth test is the only thing that can keep it from covering the first.
    builder.Draw(commandList.Get(), farSphere);

    Assert::IsTrue(SUCCEEDED(commandList->Close()));
    ID3D12CommandList* const lists[] = {commandList.Get()};
    device.Queue()->ExecuteCommandLists(1, lists);

    ComPtr<ID3D12Fence> fence;
    Assert::IsTrue(SUCCEEDED(device.Device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))));
    const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Assert::IsNotNull(event);
    Assert::IsTrue(SUCCEEDED(device.Queue()->Signal(fence.Get(), 1)));
    Assert::IsTrue(SUCCEEDED(fence->SetEventOnCompletion(1, event)));
    Assert::AreEqual(static_cast<DWORD>(WAIT_OBJECT_0), WaitForSingleObject(event, INFINITE));
    CloseHandle(event);

    std::vector<std::uint32_t> pixels;
    Assert::IsTrue(scene.ReadBack(pixels), L"the readback failed");

    const std::uint32_t center = pixels[PixelIndex(Neuron::SCREEN_WIDTH_PIXELS / 2, Neuron::SCREEN_HEIGHT_PIXELS / 2)];
    Assert::AreEqual(NEAR_COLOR, center, L"the far sphere covered the near one: the depth test did not reject it");

    // And something was actually drawn, so a black screen cannot pass this by accident.
    std::size_t painted = 0;
    for (const std::uint32_t pixel : pixels)
    {
      if (pixel != BACKGROUND)
      {
        ++painted;
      }
    }
    Assert::IsTrue(painted > 10000, L"almost nothing was drawn; the pass did not run");
  }
};

} // namespace NeuronClientTests
