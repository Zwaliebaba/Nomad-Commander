// Tests/NeuronClientTests/PrimitivePipelineTests.cpp
#include "pch.h"
#include "PrimitivePipeline.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronClientTests
{

namespace
{

/// fxc writes a DXBC container, whose first four bytes spell its name. That is the whole of what this suite can know
/// about a shader without a device; NC-021 and NC-022 draw with them.
bool IsDxbcContainer(std::span<const std::byte> _bytes)
{
  return _bytes.size() > 4 && std::to_integer<unsigned char>(_bytes[0]) == 'D' && std::to_integer<unsigned char>(_bytes[1]) == 'X' &&
         std::to_integer<unsigned char>(_bytes[2]) == 'B' && std::to_integer<unsigned char>(_bytes[3]) == 'C';
}

} // namespace

TEST_CLASS(PrimitivePipelineTests)
{
public:
  TEST_METHOD(VertexShaderIsCompiledIntoTheBinary)
  {
    Assert::IsTrue(IsDxbcContainer(Neuron::PrimitiveVertexShader()));
  }

  TEST_METHOD(PixelShaderIsCompiledIntoTheBinary)
  {
    Assert::IsTrue(IsDxbcContainer(Neuron::PrimitivePixelShader()));
  }
};

} // namespace NeuronClientTests
