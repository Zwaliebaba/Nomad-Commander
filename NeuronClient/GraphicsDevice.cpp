// NeuronClient/GraphicsDevice.cpp
#include "pch.h"
#include "GraphicsDevice.h"
#include "Debug.h"

#include <cwchar>

// R14: the Windows SDK and nothing else. These three are what d3d12.h and dxgi1_6.h are declared against.
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

namespace Neuron
{

namespace
{

using Microsoft::WRL::ComPtr;

/// Turns the debug layer on before the device is created, which is the only moment it can be turned on.
void EnableDebugLayer() noexcept
{
  ComPtr<ID3D12Debug> debug;
  if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))))
  {
    debug->EnableDebugLayer();
  }
}

/// Makes the debug layer stop at the frame that caused a problem rather than at the end of a log nobody reads.
void BreakOnDebugLayerErrors(ID3D12Device* _device) noexcept
{
  ComPtr<ID3D12InfoQueue> infoQueue;
  if (FAILED(_device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
  {
    return;
  }
  // NOMAD_VERIFY rather than a bare call: these return an HRESULT that nothing else would look at, and a failure here
  // means the next debug-layer error passes silently, which is worth an assert in _DEBUG.
  NOMAD_VERIFY(SUCCEEDED(infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE)));
  NOMAD_VERIFY(SUCCEEDED(infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE)));
}

/// The highest shader model the device supports.
///
/// CheckFeatureSupport answers by lowering the model it was given, but only as far as a model the RUNTIME knows: ask
/// for one it has never heard of and it returns E_INVALIDARG instead. So the ask walks down until one is answered,
/// which is the pattern the D3D12 documentation gives for exactly this.
[[nodiscard]] D3D_SHADER_MODEL HighestSupportedShaderModel(ID3D12Device* _device) noexcept
{
  constexpr D3D_SHADER_MODEL MODELS[] = {D3D_SHADER_MODEL_6_7, D3D_SHADER_MODEL_6_6, D3D_SHADER_MODEL_6_5,
                                         D3D_SHADER_MODEL_6_4, D3D_SHADER_MODEL_6_3, D3D_SHADER_MODEL_6_2,
                                         D3D_SHADER_MODEL_6_1, D3D_SHADER_MODEL_6_0, D3D_SHADER_MODEL_5_1};
  for (const D3D_SHADER_MODEL model : MODELS)
  {
    D3D12_FEATURE_DATA_SHADER_MODEL query{};
    query.HighestShaderModel = model;
    if (SUCCEEDED(_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &query, sizeof query)))
    {
      return query.HighestShaderModel;
    }
  }
  return D3D_SHADER_MODEL_5_1;
}

/// The first hardware adapter that makes a feature-level-11.0 device, in DXGI's high-performance order.
[[nodiscard]] bool CreateHardwareDevice(IDXGIFactory6* _factory, ComPtr<ID3D12Device>& _outDevice,
                                        DXGI_ADAPTER_DESC1& _outDescription) noexcept
{
  ComPtr<IDXGIAdapter1> adapter;
  for (UINT index = 0;
       _factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND;
       ++index)
  {
    DXGI_ADAPTER_DESC1 description{};
    if (FAILED(adapter->GetDesc1(&description)))
    {
      adapter.Reset();
      continue;
    }
    // The software adapter is reached deliberately or not at all; this loop is the hardware one.
    if ((description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) == 0 &&
        SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&_outDevice))))
    {
      _outDescription = description;
      return true;
    }
    adapter.Reset();
  }
  return false;
}

} // namespace

bool GraphicsDevice::Create(const Desc& _desc, GraphicsDevice& _outDevice) noexcept
{
  NOMAD_ASSERT(_outDevice.m_device == nullptr);
  _outDevice.m_fault = DeviceFault::None;
  _outDevice.m_result = S_OK;

  UINT factoryFlags = 0;
#if defined(_DEBUG)
  if (_desc.enableDebugLayer)
  {
    EnableDebugLayer();
    factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
  }
#endif

  _outDevice.m_result = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&_outDevice.m_factory));
  if (FAILED(_outDevice.m_result))
  {
    _outDevice.m_fault = DeviceFault::FactoryCreation;
    return false;
  }

  DXGI_ADAPTER_DESC1 description{};
  bool haveDevice = false;
  if (!_desc.useWarp)
  {
    haveDevice = CreateHardwareDevice(_outDevice.m_factory.Get(), _outDevice.m_device, description);
  }
  if (!haveDevice)
  {
    // Asked for, or nothing else would do it. A machine with no usable GPU still runs, which is also what lets the
    // suite exercise all of this on a build agent (Plan/Roadmap.md A12).
    ComPtr<IDXGIAdapter> warpAdapter;
    _outDevice.m_result = _outDevice.m_factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter));
    if (FAILED(_outDevice.m_result))
    {
      _outDevice.m_fault = DeviceFault::NoAdapter;
      return false;
    }
    _outDevice.m_result = D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&_outDevice.m_device));
    if (FAILED(_outDevice.m_result))
    {
      _outDevice.m_fault = DeviceFault::DeviceCreation;
      return false;
    }
    ComPtr<IDXGIAdapter1> warpDescribed;
    if (SUCCEEDED(warpAdapter.As(&warpDescribed)))
    {
      NOMAD_VERIFY(SUCCEEDED(warpDescribed->GetDesc1(&description)));
    }
    _outDevice.m_isWarp = true;
  }

  // wcsncpy_s rather than a std::wstring: Create is noexcept, and an allocation in a noexcept function is
  // std::terminate on a machine low on memory (the defect NC-020 round 5 found in this same pattern).
  wcsncpy_s(_outDevice.m_adapterName, description.Description, _TRUNCATE);

#if defined(_DEBUG)
  if (_desc.enableDebugLayer)
  {
    BreakOnDebugLayerErrors(_outDevice.m_device.Get());
  }
#endif

  // ADR-011 put every shader in this tree at model 6.7. A device below that cannot create one pipeline here, so it is
  // refused now, by name, rather than at the first CreateGraphicsPipelineState with an HRESULT nobody can read.
  _outDevice.m_shaderModel = HighestSupportedShaderModel(_outDevice.m_device.Get());
  if (_outDevice.m_shaderModel < REQUIRED_SHADER_MODEL)
  {
    _outDevice.m_fault = DeviceFault::ShaderModelTooLow;
    _outDevice.m_result = E_FAIL;
    return false;
  }

  D3D12_COMMAND_QUEUE_DESC queueDesc{};
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
  queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queueDesc.NodeMask = 0;
  _outDevice.m_result = _outDevice.m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&_outDevice.m_queue));
  if (FAILED(_outDevice.m_result))
  {
    _outDevice.m_fault = DeviceFault::CommandQueue;
    return false;
  }
  return true;
}

} // namespace Neuron
