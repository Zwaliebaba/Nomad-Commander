// Tests/GameLogicTests/UniverseGeneratorTests.cpp
#include "pch.h"
#include "ByteReader.h"
#include "ByteWriter.h"
#include "UniverseGenerator.h"

#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

/// GDD §15's v0.1 shape, and the Milestone 2 shape the generator must also reach without being resized for it.
constexpr std::uint32_t V01_SYSTEMS = 10;
constexpr std::uint32_t V01_EMPIRES = 3;
constexpr std::uint32_t MILESTONE2_SYSTEMS = 20;
constexpr std::uint32_t MILESTONE2_EMPIRES = 5;

/// A hundred consecutive seeds, which is what the acceptance criterion asks the role invariants to hold over.
constexpr std::uint64_t SEEDS = 100;

[[nodiscard]] Nomad::World Generate(std::uint64_t _seed, std::uint32_t _systems, std::uint32_t _empires)
{
  Nomad::World world{_seed};
  const Nomad::UniverseGenerator::Desc desc{_systems, _empires};
  Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, world), (L"the generator refused seed " + std::to_wstring(_seed)).c_str());
  return world;
}

/// Reachability from one system, counting what it can see. Written here rather than called from the World so that the
/// invariants below are checked against an independent walk of the graph and not against the same code twice.
[[nodiscard]] std::uint32_t ReachableCount(const Nomad::World& _world, Nomad::SystemId _start, Nomad::SystemId _excluded)
{
  const std::uint32_t count = _world.Systems().Count();
  std::vector<bool> seen(count, false);
  std::vector<Nomad::SystemId> stack{_start};
  seen[_start.Index()] = true;
  std::uint32_t reached = 1;
  while (!stack.empty())
  {
    const Nomad::SystemId system = stack.back();
    stack.pop_back();
    for (const Nomad::LaneId laneId : _world.Systems().Get(system).lanes)
    {
      const Nomad::SystemId neighbor = _world.Lanes().Get(laneId).Other(system);
      if (neighbor == _excluded || seen[neighbor.Index()])
      {
        continue;
      }
      seen[neighbor.Index()] = true;
      ++reached;
      stack.push_back(neighbor);
    }
  }
  return reached;
}

[[nodiscard]] Nomad::SystemId FirstWithRole(const Nomad::World& _world, Nomad::SystemRole _role)
{
  for (std::uint32_t index = 0; index < _world.Systems().Count(); ++index)
  {
    if (_world.Systems().Get(Nomad::SystemId::FromIndex(index)).role == _role)
    {
      return Nomad::SystemId::FromIndex(index);
    }
  }
  return Nomad::SystemId{};
}

[[nodiscard]] std::vector<std::byte> Saved(const Nomad::World& _world)
{
  Neuron::ByteWriter writer;
  _world.Serialize(writer);
  const std::span<const std::byte> bytes = writer.Bytes();
  return std::vector<std::byte>{bytes.begin(), bytes.end()};
}

} // namespace

TEST_CLASS(UniverseGeneratorTests)
{
public:
  TEST_METHOD(EverySeedGivesAConnectedMapWithEveryRolePresent)
  {
    // The acceptance criterion, over a hundred consecutive seeds: ten systems and three empires, connected, with all
    // eight of GDD §7's roles on the map. A generator that produced a good map most of the time would be a generator
    // whose sandbox occasionally has no chokepoint in it, and nobody would notice until a scenario read oddly.
    for (std::uint64_t seed = 0; seed < SEEDS; ++seed)
    {
      const Nomad::World world = Generate(seed, V01_SYSTEMS, V01_EMPIRES);
      const std::wstring at = L" at seed " + std::to_wstring(seed);

      Assert::AreEqual(V01_SYSTEMS, world.Systems().Count(), (L"wrong system count" + at).c_str());
      Assert::AreEqual(V01_EMPIRES, world.Empires().Count(), (L"wrong empire count" + at).c_str());
      Assert::AreEqual(V01_SYSTEMS, ReachableCount(world, Nomad::SystemId::FromIndex(0), Nomad::SystemId{}),
                       (L"the map is not connected" + at).c_str());

      bool present[Nomad::SYSTEM_ROLE_COUNT] = {};
      for (const Nomad::StarSystem& system : world.Systems().Rows())
      {
        present[static_cast<std::uint32_t>(system.role)] = true;
      }
      for (std::uint32_t role = 0; role < Nomad::SYSTEM_ROLE_COUNT; ++role)
      {
        Assert::IsTrue(present[role], (L"role " + std::to_wstring(role) + L" is missing" + at).c_str());
      }
    }
  }

  TEST_METHOD(TheStructuralRolesAreTrueOfTheGraphAndNotJustLabels)
  {
    // ADR-017's whole point. Four roles are claims about the shape of the map, and each is checked here by walking
    // the graph rather than by reading the role back.
    for (std::uint64_t seed = 0; seed < SEEDS; ++seed)
    {
      const Nomad::World world = Generate(seed, V01_SYSTEMS, V01_EMPIRES);
      const std::wstring at = L" at seed " + std::to_wstring(seed);

      const Nomad::SystemId chokepoint = FirstWithRole(world, Nomad::SystemRole::Chokepoint);
      const Nomad::SystemId deadEnd = FirstWithRole(world, Nomad::SystemRole::DeadEnd);
      const Nomad::SystemId crossroads = FirstWithRole(world, Nomad::SystemRole::Crossroads);
      const Nomad::SystemId bypass = FirstWithRole(world, Nomad::SystemRole::Bypass);
      Assert::IsTrue(chokepoint.IsValid() && deadEnd.IsValid() && crossroads.IsValid() && bypass.IsValid(),
                     (L"a structural role is missing" + at).c_str());

      // A dead end has exactly one lane.
      Assert::AreEqual(std::size_t{1}, world.Systems().Get(deadEnd).lanes.size(), (L"the dead end is not a dead end" + at).c_str());

      // A crossroads has four or more.
      Assert::IsTrue(world.Systems().Get(crossroads).lanes.size() >= 4, (L"the crossroads has fewer than four lanes" + at).c_str());

      // Taking the chokepoint out disconnects the map: something that was reachable no longer is.
      Nomad::SystemId elsewhere{};
      for (std::uint32_t index = 0; index < world.Systems().Count(); ++index)
      {
        const Nomad::SystemId candidate = Nomad::SystemId::FromIndex(index);
        if (candidate != chokepoint)
        {
          elsewhere = candidate;
          break;
        }
      }
      Assert::IsTrue(ReachableCount(world, elsewhere, chokepoint) < world.Systems().Count() - 1,
                     (L"removing the chokepoint left the map connected, so it is not one" + at).c_str());

      // And taking the bypass out does not: there is a way around it, which is what a bypass is.
      Nomad::SystemId pastBypass{};
      for (std::uint32_t index = 0; index < world.Systems().Count(); ++index)
      {
        const Nomad::SystemId candidate = Nomad::SystemId::FromIndex(index);
        if (candidate != bypass)
        {
          pastBypass = candidate;
          break;
        }
      }
      Assert::AreEqual(world.Systems().Count() - 1, ReachableCount(world, pastBypass, bypass),
                       (L"removing the bypass disconnected the map, so it is a chokepoint" + at).c_str());
    }
  }

  TEST_METHOD(NoLaneLeavesTheTwoToFourHourBand)
  {
    // GDD §7's starting clock: "a jump takes two to four real hours depending on the lane". A lane outside the band
    // is a map that does not pace the way the design says it does.
    for (std::uint64_t seed = 0; seed < SEEDS; ++seed)
    {
      const Nomad::World world = Generate(seed, V01_SYSTEMS, V01_EMPIRES);
      Assert::IsTrue(world.Lanes().Count() > 0, L"a map with no lanes");
      for (const Nomad::Lane& lane : world.Lanes().Rows())
      {
        Assert::IsTrue(lane.jumpTicks >= Nomad::LANE_MIN_JUMP_TICKS && lane.jumpTicks <= Nomad::LANE_MAX_JUMP_TICKS,
                       (L"a lane of " + std::to_wstring(lane.jumpTicks) + L" ticks at seed " + std::to_wstring(seed)).c_str());
        Assert::IsTrue(lane.first != lane.second, L"a lane joins a system to itself");
      }
    }
  }

  TEST_METHOD(TwoRunsFromOneSeedAreByteIdentical)
  {
    // R16. The generator draws from the world's Generation stream, so this also says that placing systems does not
    // depend on anything outside the seed.
    for (std::uint64_t seed = 0; seed < 20; ++seed)
    {
      const Nomad::World left = Generate(seed, V01_SYSTEMS, V01_EMPIRES);
      const Nomad::World right = Generate(seed, V01_SYSTEMS, V01_EMPIRES);
      Assert::AreEqual(left.Hash(), right.Hash(), (L"two runs of seed " + std::to_wstring(seed) + L" differ").c_str());
      Assert::IsTrue(Saved(left) == Saved(right), L"the stores differ byte for byte");
    }

    // And two different seeds do not: a generator that ignored its seed would pass everything above.
    Assert::AreNotEqual(Generate(1, V01_SYSTEMS, V01_EMPIRES).Hash(), Generate(2, V01_SYSTEMS, V01_EMPIRES).Hash());
  }

  TEST_METHOD(EveryEmpireHasAHomeWithAShipyardAndContiguousHoldings)
  {
    for (std::uint64_t seed = 0; seed < SEEDS; ++seed)
    {
      const Nomad::World world = Generate(seed, V01_SYSTEMS, V01_EMPIRES);
      const std::wstring at = L" at seed " + std::to_wstring(seed);

      std::uint32_t unowned = 0;
      for (const Nomad::StarSystem& system : world.Systems().Rows())
      {
        if (!system.owner.IsValid())
        {
          ++unowned;
        }
      }
      // GDD §8's contraction has already happened: one or two systems belong to nobody.
      Assert::AreEqual(Nomad::UniverseGenerator::HarborCount(V01_SYSTEMS), unowned, (L"wrong number of harbours" + at).c_str());

      for (std::uint32_t index = 0; index < world.Empires().Count(); ++index)
      {
        const Nomad::Empire& empire = world.Empires().Get(Nomad::EmpireId::FromIndex(index));
        Assert::IsTrue(empire.homeSystem.IsValid(), (L"an empire has no home" + at).c_str());
        Assert::IsTrue(world.Systems().Get(empire.homeSystem).hasShipyard, (L"a home has no shipyard" + at).c_str());
        Assert::IsTrue(world.Systems().Get(empire.homeSystem).owner == Nomad::EmpireId::FromIndex(index),
                       (L"a home is not held by its own empire" + at).c_str());

        // Contiguous: every system held is reachable from the home through systems this empire also holds.
        for (const Nomad::SystemId held : empire.systemsHeld)
        {
          std::vector<Nomad::SystemId> route;
          Assert::IsTrue(world.ShortestRoute(empire.homeSystem, held, route), (L"a holding is unreachable" + at).c_str());
        }
      }
    }
  }

  TEST_METHOD(TheJumpCountIsTheRoutesLengthAndAgreesWithItself)
  {
    const Nomad::World world = Generate(7, V01_SYSTEMS, V01_EMPIRES);
    std::vector<Nomad::SystemId> route;
    for (std::uint32_t from = 0; from < world.Systems().Count(); ++from)
    {
      for (std::uint32_t to = 0; to < world.Systems().Count(); ++to)
      {
        const auto fromId = Nomad::SystemId::FromIndex(from);
        const auto toId = Nomad::SystemId::FromIndex(to);
        Assert::IsTrue(world.ShortestRoute(fromId, toId, route), L"the map is connected, so every route exists");
        Assert::IsTrue(route.front() == fromId && route.back() == toId, L"a route does not start and end where it was asked to");
        Assert::AreEqual(world.JumpsBetween(fromId, toId), static_cast<std::uint32_t>(route.size() - 1),
                         L"JumpsBetween disagrees with the route it is counting");

        // Every step of the route is one lane.
        for (std::size_t step = 0; step + 1 < route.size(); ++step)
        {
          Assert::AreEqual(1u, world.JumpsBetween(route[step], route[step + 1]), L"a step of a route is not one jump");
        }
      }
      Assert::AreEqual(0u, world.JumpsBetween(Nomad::SystemId::FromIndex(from), Nomad::SystemId::FromIndex(from)));
    }
  }

  TEST_METHOD(TheMilestoneTwoShapeGeneratesToo)
  {
    // A13 and GDD Milestone 2: twenty systems and five empires. The generator must not be sized to ten, and finding
    // that out now costs one test rather than a rewrite later.
    for (std::uint64_t seed = 0; seed < 20; ++seed)
    {
      const Nomad::World world = Generate(seed, MILESTONE2_SYSTEMS, MILESTONE2_EMPIRES);
      Assert::AreEqual(MILESTONE2_SYSTEMS, world.Systems().Count());
      Assert::AreEqual(MILESTONE2_EMPIRES, world.Empires().Count());
      Assert::AreEqual(MILESTONE2_SYSTEMS, ReachableCount(world, Nomad::SystemId::FromIndex(0), Nomad::SystemId{}));
    }
  }

  TEST_METHOD(AnImpossibleDescriptionIsRefusedRatherThanApproximated)
  {
    Nomad::World world{1};
    const Nomad::UniverseGenerator::Desc tooFew{Nomad::UniverseGenerator::MIN_SYSTEM_COUNT - 1, 1};
    Assert::IsFalse(Nomad::UniverseGenerator::Generate(tooFew, world), L"eight roles cannot fit in seven systems");
    Assert::AreEqual(0u, world.Systems().Count(), L"a refused generation left systems behind");

    const Nomad::UniverseGenerator::Desc tooManyEmpires{Nomad::UniverseGenerator::MIN_SYSTEM_COUNT, 100};
    Assert::IsFalse(Nomad::UniverseGenerator::Generate(tooManyEmpires, world), L"more empires than systems was accepted");

    const Nomad::UniverseGenerator::Desc noEmpires{V01_SYSTEMS, 0};
    Assert::IsFalse(Nomad::UniverseGenerator::Generate(noEmpires, world), L"a map with no empires was accepted");

    // And a world that already holds a map is refused rather than doubled.
    Nomad::World populated = Generate(3, V01_SYSTEMS, V01_EMPIRES);
    const Nomad::UniverseGenerator::Desc again{V01_SYSTEMS, V01_EMPIRES};
    Assert::IsFalse(Nomad::UniverseGenerator::Generate(again, populated), L"a second generation into one world was accepted");
  }

  TEST_METHOD(AGeneratedWorldSurvivesTheStore)
  {
    // The map is the largest thing in a store so far, and NC-043 is about to reload one mid-run.
    const Nomad::World original = Generate(11, V01_SYSTEMS, V01_EMPIRES);
    const std::vector<std::byte> bytes = Saved(original);

    Nomad::World restored{0};
    Neuron::ByteReader reader{bytes};
    Assert::IsTrue(restored.Deserialize(reader), L"a generated world could not be read back");
    Assert::AreEqual(std::size_t{0}, reader.Remaining());
    Assert::AreEqual(original.Hash(), restored.Hash());
    Assert::AreEqual(original.Systems().Count(), restored.Systems().Count());
    Assert::AreEqual(original.Lanes().Count(), restored.Lanes().Count());

    // The graph survived, not just the rows: a route through the restored map is the same route.
    std::vector<Nomad::SystemId> before;
    std::vector<Nomad::SystemId> after;
    Assert::IsTrue(original.ShortestRoute(Nomad::SystemId::FromIndex(0), Nomad::SystemId::FromIndex(V01_SYSTEMS - 1), before));
    Assert::IsTrue(restored.ShortestRoute(Nomad::SystemId::FromIndex(0), Nomad::SystemId::FromIndex(V01_SYSTEMS - 1), after));
    Assert::IsTrue(before == after, L"the restored map routes differently");
  }
};

} // namespace GameLogicTests
