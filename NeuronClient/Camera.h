// NeuronClient/Camera.h
#pragma once

#include "NeuronCore.h"

#include <DirectXMath.h>

namespace Neuron
{

/// Where the map is looked at from, as a plain value. `MapScreen` owns one and sets it.
///
/// **There is no controller, no interpolation and no scene graph** (NC-027's out-of-scope list). A camera here is six
/// numbers and a function that multiplies two matrices; if the map ever pans, something outside this type will move
/// these fields, and that will be its own decision.
///
/// `DirectXMath` is Windows SDK content, which R14's D3D paragraph admits by name — unlike `d3dx12.h` and DirectXTK,
/// which it excludes.
struct Camera
{
  DirectX::XMFLOAT3 positionUnits{0.0f, 0.0f, -10.0f};
  DirectX::XMFLOAT3 targetUnits{0.0f, 0.0f, 0.0f};
  DirectX::XMFLOAT3 up{0.0f, 1.0f, 0.0f};

  /// Vertical field of view. A narrow one keeps a map readable: wide angles put the systems at the edge of the screen
  /// through a lens the player has to correct for, and GDD §13 wants the map read rather than admired.
  float verticalFieldOfViewRadians = DirectX::XM_PIDIV4;

  float nearPlaneUnits = 0.1f;
  float farPlaneUnits = 1000.0f;

  /// Width over height. The screen is always 1920x1080 (R12), so this is always 16:9 — but it is a field rather than
  /// a constant because a test sets it and because the one place that would ever differ is a test.
  float aspectRatio = 16.0f / 9.0f;

  /// View times projection, left-handed, row-major, ready for a root constant.
  [[nodiscard]] DirectX::XMFLOAT4X4 ViewProjection() const noexcept
  {
    const DirectX::XMVECTOR eye = DirectX::XMLoadFloat3(&positionUnits);
    const DirectX::XMVECTOR at = DirectX::XMLoadFloat3(&targetUnits);
    const DirectX::XMVECTOR upward = DirectX::XMLoadFloat3(&up);
    const DirectX::XMMATRIX view = DirectX::XMMatrixLookAtLH(eye, at, upward);
    const DirectX::XMMATRIX projection =
      DirectX::XMMatrixPerspectiveFovLH(verticalFieldOfViewRadians, aspectRatio, nearPlaneUnits, farPlaneUnits);
    DirectX::XMFLOAT4X4 result;
    // Transposed on the way out: HLSL reads a float4x4 constant column-major by default, and the shader multiplies
    // the position on the left. Doing it here means the shader has no transpose in it and no mul order to remember.
    DirectX::XMStoreFloat4x4(&result, DirectX::XMMatrixTranspose(DirectX::XMMatrixMultiply(view, projection)));
    return result;
  }
};

} // namespace Neuron
