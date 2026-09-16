// Tests/NeuronServerTests/SuiteSmoke.cpp
#include "pch.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronServerTests
{

/// The placeholder that keeps vstest from reporting "no tests found" as a pass over a suite nobody exercised
/// (AGENTS.md §3). Delete it when the first real test lands, never before.
TEST_CLASS(SuiteSmoke)
{
public:
  TEST_METHOD(SuiteIsDiscovered)
  {
    Assert::IsTrue(true);
  }
};

} // namespace NeuronServerTests
