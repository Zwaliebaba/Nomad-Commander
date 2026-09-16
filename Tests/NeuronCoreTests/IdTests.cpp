// Tests/NeuronCoreTests/IdTests.cpp
#include "pch.h"
#include "Id.h"
#include <type_traits>
#include <unordered_set>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{

namespace
{

struct FleetTag
{
};
struct CompanyTag
{
};
using FleetId = Neuron::Id<FleetTag>;
using CompanyId = Neuron::Id<CompanyTag>;

// Two tags are two types, and neither is an integer (AGENTS.md R22): the compiler keeps them apart, at compile time.
static_assert(!std::is_convertible_v<FleetId, CompanyId>);
static_assert(!std::is_convertible_v<CompanyId, FleetId>);
static_assert(!std::is_convertible_v<FleetId, std::uint32_t>);
static_assert(!std::is_convertible_v<std::uint32_t, FleetId>);
static_assert(!std::is_constructible_v<FleetId, std::uint32_t>);
static_assert(!std::is_constructible_v<FleetId, CompanyId>);

// The sentinel: a default Id is invalid, anything below the sentinel is valid, the sentinel itself is not.
static_assert(!FleetId{}.IsValid());
static_assert(FleetId::FromIndex(0).IsValid());
static_assert(FleetId::FromIndex(FleetId::INVALID_INDEX - 1).IsValid());
static_assert(!FleetId::FromIndex(FleetId::INVALID_INDEX).IsValid());
static_assert(FleetId::FromIndex(7).Index() == 7);
static_assert(FleetId::FromIndex(3) == FleetId::FromIndex(3));
static_assert(FleetId::FromIndex(1) < FleetId::FromIndex(2));
static_assert(FleetId::FromIndex(2) < FleetId{});

} // namespace

} // namespace NeuronCoreTests

// The documented pattern for a default hasher: one full explicit specialization per alias, where the alias lives.
template <> struct std::hash<NeuronCoreTests::FleetId> : NeuronCoreTests::FleetId::Hash
{
};

namespace NeuronCoreTests
{

TEST_CLASS(IdTests)
{
public:
  TEST_METHOD(ADefaultIdIsInvalidAndEqualToEveryOtherDefaultId)
  {
    Assert::IsFalse(FleetId{}.IsValid());
    Assert::IsTrue(FleetId{} == FleetId{});
    Assert::IsTrue(FleetId{}.Index() == FleetId::INVALID_INDEX);
  }

  TEST_METHOD(FromIndexRoundTrips)
  {
    const FleetId id = FleetId::FromIndex(42);
    Assert::IsTrue(id.IsValid());
    Assert::AreEqual(std::uint32_t{42}, id.Index());
  }

  TEST_METHOD(OrderingFollowsTheIndex)
  {
    Assert::IsTrue(FleetId::FromIndex(1) < FleetId::FromIndex(2));
    Assert::IsTrue(FleetId::FromIndex(2) > FleetId::FromIndex(1));
    Assert::IsTrue(FleetId::FromIndex(5) <= FleetId::FromIndex(5));
    Assert::IsFalse(FleetId::FromIndex(5) != FleetId::FromIndex(5));
  }

  TEST_METHOD(HashDistinguishesIdsAndAgreesWithEquality)
  {
    std::unordered_set<FleetId, FleetId::Hash> named;
    named.insert(FleetId::FromIndex(1));
    named.insert(FleetId::FromIndex(2));
    named.insert(FleetId::FromIndex(1));
    Assert::AreEqual(std::size_t{2}, named.size());
    Assert::IsTrue(named.contains(FleetId::FromIndex(2)));
    Assert::IsFalse(named.contains(FleetId::FromIndex(3)));

    std::unordered_set<FleetId> defaulted;
    defaulted.insert(FleetId::FromIndex(1));
    defaulted.insert(FleetId::FromIndex(1));
    Assert::AreEqual(std::size_t{1}, defaulted.size());
    Assert::AreEqual(FleetId::Hash{}(FleetId::FromIndex(9)), std::hash<FleetId>{}(FleetId::FromIndex(9)));
  }
};

} // namespace NeuronCoreTests
