// Tests/GameLogicTests/WorldTests.cpp
#include "pch.h"
#include "ByteReader.h"
#include "ByteWriter.h"
#include "World.h"

#include <string>
#include <type_traits>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

/// A world with one of everything and two companies, because R22 makes the nomad a table rather than a singleton and
/// a schema that only ever held one row would not prove that.
///
/// Every field is given a value that is not its default: a round trip that forgot a field would otherwise pass by
/// writing a zero and reading a zero back.
[[nodiscard]] Nomad::World PopulatedWorld()
{
  Nomad::World world{0x5EEDu};

  const Nomad::CharacterId varik = world.Characters().Add(Nomad::Character{
    "Varik", Nomad::CharacterRole::Admiral, Nomad::Allegiance{Nomad::EmpireId::FromIndex(0), Nomad::CompanyId{}}, 3, true});
  const Nomad::CharacterId officer = world.Characters().Add(Nomad::Character{
    "Sedu Compact Liaison", Nomad::CharacterRole::Officer, Nomad::Allegiance{Nomad::EmpireId{}, Nomad::CompanyId::FromIndex(0)}, 1, true});

  const Nomad::EmpireId varn = world.Empires().Add(Nomad::Empire{"Varn",
                                                                 varik,
                                                                 Nomad::SystemId::FromIndex(2),
                                                                 0,
                                                                 {Nomad::SystemId::FromIndex(2), Nomad::SystemId::FromIndex(3)},
                                                                 {Nomad::FleetId::FromIndex(0)},
                                                                 {},
                                                                 {},
                                                                 true});

  Nomad::Company first;
  first.name = "Sedu Compact";
  first.mothership = Nomad::Mothership{Nomad::SystemId::FromIndex(1), Nomad::MothershipState::Healthy, 4, Nomad::ShipClass::Scout, 0};
  first.treasury = 12345;
  first.officers = {officer};
  first.fleets = {Nomad::FleetId::FromIndex(0)};
  first.outposts = {Nomad::OutpostId::FromIndex(0)};
  first.record = {Nomad::EventId::FromIndex(7), Nomad::EventId::FromIndex(9)};
  first.activeWindow = Nomad::ActiveWindow{8 * Neuron::TICKS_PER_HOUR, 2 * Neuron::TICKS_PER_HOUR};
  first.alive = true;
  const Nomad::CompanyId firstCompany = world.Companies().Add(first);

  // The second nomad. It exists only to prove the table holds any number of them (R22, GDD §14).
  Nomad::Company second = first;
  second.name = "Low Meridian Salvage";
  second.treasury = -400;
  second.mothership.state = Nomad::MothershipState::Damaged;
  second.alive = false;
  world.Companies().Add(second);

  Nomad::Fleet convoy;
  convoy.name = "Kessel Convoy";
  convoy.owner = varn;
  convoy.role = Nomad::FleetRole::Convoy;
  convoy.commander = varik;
  convoy.ships = Nomad::ShipCounts{{0, 0, 2, 5}};
  convoy.position = Nomad::InLane{Nomad::LaneId::FromIndex(4), Nomad::SystemId::FromIndex(2), 100, 340};
  convoy.fuel = 11;
  convoy.cargoByGood = {3, 0, 7, 1};
  convoy.marked = true;
  convoy.veterancy = Neuron::Hundredths::FromRaw(62);
  convoy.history = {Nomad::EventId::FromIndex(1)};
  convoy.alive = true;
  world.Fleets().Add(convoy);

  // A drifting fleet owned by a company, so both variant alternatives of the owner and two of the three of the
  // position are on the wire in one round trip.
  Nomad::Fleet stranded = convoy;
  stranded.name = "Ashfall Picket";
  stranded.owner = firstCompany;
  stranded.role = Nomad::FleetRole::Picket;
  stranded.position = Nomad::Drifting{Nomad::SystemId::FromIndex(5)};
  stranded.veterancy = Neuron::Hundredths::FromRaw(-5);
  stranded.alive = false;
  world.Fleets().Add(stranded);

  Nomad::Outpost outpost;
  outpost.name = "Pale Anchor Yard";
  outpost.owningCompany = firstCompany;
  outpost.owningEmpire = Nomad::EmpireId{};
  outpost.system = Nomad::SystemId::FromIndex(1);
  outpost.stockByGood = {12, 4, 0, 9};
  outpost.docked = Nomad::ShipCounts{{1, 2, 0, 0}};
  outpost.claimExpiresTick = 9 * Neuron::TICKS_PER_DAY;
  outpost.alive = true;
  world.Outposts().Add(outpost);

  for (int tick = 0; tick < 37; ++tick)
  {
    world.AdvanceTick();
  }
  return world;
}

[[nodiscard]] std::vector<std::byte> Saved(const Nomad::World& _world)
{
  Neuron::ByteWriter writer;
  _world.Serialize(writer);
  const std::span<const std::byte> bytes = writer.Bytes();
  return std::vector<std::byte>{bytes.begin(), bytes.end()};
}

} // namespace

TEST_CLASS(WorldTests)
{
public:
  TEST_METHOD(AnIdFromOneTableDoesNotIndexAnother)
  {
    // The whole point of a typed index (AGENTS.md §2): these are different types and neither converts to the other or
    // to an integer. It is a compile-time test, so its failure is a build error rather than a red test -- which is
    // what makes it worth writing, because the defect it prevents is silent.
    static_assert(!std::is_same_v<Nomad::FleetId, Nomad::CompanyId>);
    static_assert(!std::is_convertible_v<Nomad::FleetId, Nomad::CompanyId>);
    static_assert(!std::is_convertible_v<Nomad::CompanyId, Nomad::FleetId>);
    static_assert(!std::is_convertible_v<Nomad::FleetId, std::uint32_t>);
    static_assert(!std::is_constructible_v<Nomad::FleetId, std::uint32_t>);
    static_assert(!std::is_same_v<Nomad::SystemId, Nomad::LaneId>);

    // And the runtime half: an id is a handle into the table that issued it, and every table numbers from zero.
    Nomad::World world{1};
    const Nomad::CompanyId company = world.Companies().Add(Nomad::Company{});
    const Nomad::FleetId fleet = world.Fleets().Add(Nomad::Fleet{});
    Assert::AreEqual(0u, company.Index());
    Assert::AreEqual(0u, fleet.Index());
    Assert::IsTrue(world.Companies().Holds(company));
    Assert::IsTrue(world.Fleets().Holds(fleet));
  }

  TEST_METHOD(TheNomadIsATableAndHoldsAsManyAsAreAdded)
  {
    // R22 and GDD §14: "the kernel treats the nomad as an entity type with any number of instances from day one".
    // There is no singleton player object anywhere for this test to be written against.
    Nomad::World world = PopulatedWorld();
    Assert::AreEqual(2u, world.Companies().Count());
    Assert::AreEqual(std::string{"Sedu Compact"}, world.Companies().Get(Nomad::CompanyId::FromIndex(0)).name);
    Assert::AreEqual(std::string{"Low Meridian Salvage"}, world.Companies().Get(Nomad::CompanyId::FromIndex(1)).name);
  }

  TEST_METHOD(ARoundTripThroughBytesGivesBackTheSameWorld)
  {
    const Nomad::World original = PopulatedWorld();
    const std::vector<std::byte> bytes = Saved(original);

    Nomad::World restored{0};
    Neuron::ByteReader reader{bytes};
    Assert::IsTrue(restored.Deserialize(reader), L"a world this build wrote could not be read back");
    Assert::AreEqual(std::size_t{0}, reader.Remaining(), L"the reader did not consume the whole store");

    Assert::AreEqual(original.Hash(), restored.Hash(), L"the hashes differ after a round trip");
    Assert::AreEqual(original.Seed(), restored.Seed());
    Assert::AreEqual(original.CurrentTick(), restored.CurrentTick());

    // The hash is taken over the bytes, so equal hashes and equal stores are one statement. These spot checks are
    // here for the failure message: a hash that differs says nothing about which field went missing.
    Assert::AreEqual(original.Companies().Count(), restored.Companies().Count());
    Assert::AreEqual(original.Fleets().Count(), restored.Fleets().Count());
    Assert::AreEqual(original.Characters().Count(), restored.Characters().Count());
    Assert::AreEqual(original.Outposts().Count(), restored.Outposts().Count());
    Assert::AreEqual(original.Empires().Count(), restored.Empires().Count());

    const Nomad::Fleet& before = original.Fleets().Get(Nomad::FleetId::FromIndex(0));
    const Nomad::Fleet& after = restored.Fleets().Get(Nomad::FleetId::FromIndex(0));
    Assert::AreEqual(before.name, after.name);
    Assert::IsTrue(before.ships == after.ships, L"the ship counts did not survive");
    Assert::IsTrue(before.veterancy.Raw() == after.veterancy.Raw(), L"the veterancy did not survive");
    Assert::IsTrue(std::holds_alternative<Nomad::InLane>(after.position), L"the fleet position changed alternative");
    Assert::AreEqual(std::get<Nomad::InLane>(before.position).arrivalTick, std::get<Nomad::InLane>(after.position).arrivalTick);
    Assert::IsTrue(std::holds_alternative<Nomad::EmpireId>(after.owner), L"the fleet owner changed alternative");

    const Nomad::Fleet& strandedAfter = restored.Fleets().Get(Nomad::FleetId::FromIndex(1));
    Assert::IsTrue(std::holds_alternative<Nomad::Drifting>(strandedAfter.position));
    Assert::IsTrue(std::holds_alternative<Nomad::CompanyId>(strandedAfter.owner));
    Assert::IsFalse(strandedAfter.alive, L"a dead fleet keeps its row and its flag");
  }

  TEST_METHOD(EveryFieldReachesTheStore)
  {
    // The round trip above proves the bytes survive a write and a read. This proves the *hash* notices a field: for
    // each mutation, the world must hash differently from the one it was copied from. A field that Serialize forgot
    // would fail here and nowhere else.
    const Nomad::World base = PopulatedWorld();
    const std::uint64_t baseHash = base.Hash();

    Nomad::World tick = base;
    tick.AdvanceTick();
    Assert::AreNotEqual(baseHash, tick.Hash(), L"the clock does not reach the store");

    Nomad::World treasury = base;
    treasury.Companies().Get(Nomad::CompanyId::FromIndex(0)).treasury += 1;
    Assert::AreNotEqual(baseHash, treasury.Hash(), L"a treasury does not reach the store");

    Nomad::World fuel = base;
    fuel.Fleets().Get(Nomad::FleetId::FromIndex(0)).fuel += 1;
    Assert::AreNotEqual(baseHash, fuel.Hash(), L"a fleet's fuel does not reach the store");

    Nomad::World veterancy = base;
    veterancy.Fleets().Get(Nomad::FleetId::FromIndex(0)).veterancy = Neuron::Hundredths::FromRaw(63);
    Assert::AreNotEqual(baseHash, veterancy.Hash(), L"a veterancy does not reach the store");

    Nomad::World capacity = base;
    capacity.Characters().Get(Nomad::CharacterId::FromIndex(0)).commandCapacity += 1;
    Assert::AreNotEqual(baseHash, capacity.Hash(), L"a command capacity does not reach the store");

    Nomad::World claim = base;
    claim.Outposts().Get(Nomad::OutpostId::FromIndex(0)).claimExpiresTick += 1;
    Assert::AreNotEqual(baseHash, claim.Hash(), L"an outpost's claim does not reach the store");

    Nomad::World slot = base;
    slot.Empires().Get(Nomad::EmpireId::FromIndex(0)).colorSlot += 1;
    Assert::AreNotEqual(baseHash, slot.Hash(), L"an empire's colour slot does not reach the store");

    Nomad::World drawn = base;
    (void)drawn.RandomFor(Nomad::RandomStream::Battle).Next();
    Assert::AreNotEqual(baseHash, drawn.Hash(), L"a PRNG stream's state does not reach the store");
  }

  TEST_METHOD(ATruncatedStoreIsRefusedAndLeavesTheWorldAlone)
  {
    // A store that runs out half way must not leave a caller holding a partial world -- it would replay as a world
    // that never existed. Every truncation is tried, not one: the failure worth catching is the one field whose read
    // was not checked.
    const std::vector<std::byte> bytes = Saved(PopulatedWorld());
    Assert::IsTrue(bytes.size() > 64, L"the sample store is too small for this to prove anything");

    for (std::size_t length = 0; length < bytes.size(); ++length)
    {
      Nomad::World world{99};
      const std::uint64_t before = world.Hash();
      Neuron::ByteReader reader{std::span<const std::byte>{bytes.data(), length}};
      Assert::IsFalse(world.Deserialize(reader), (L"a store truncated to " + std::to_wstring(length) + L" bytes was accepted").c_str());
      Assert::AreEqual(before, world.Hash(),
                       (L"the world changed after refusing a store truncated to " + std::to_wstring(length) + L" bytes").c_str());
    }
  }

  TEST_METHOD(AStoreFromAnotherSchemaIsRefused)
  {
    std::vector<std::byte> bytes = Saved(PopulatedWorld());
    // The version is the first field (ADR-004), little-endian, so the low byte is byte zero.
    bytes[0] = static_cast<std::byte>(Nomad::World::SCHEMA_VERSION + 1);

    Nomad::World world{1};
    Neuron::ByteReader reader{bytes};
    Assert::IsFalse(world.Deserialize(reader), L"a store from another schema version was accepted");
  }

  TEST_METHOD(AnEnumeratorOutsideItsRangeIsRefused)
  {
    // A byte the enum has no name for must be refused rather than cast into the world: a MothershipState of 200 is a
    // switch nobody wrote a case for, and a store is the only place such a byte can come from.
    //
    // The offset is spelled out rather than searched for, so that a change to the layout breaks this test loudly
    // instead of leaving it quietly corrupting some other field's byte.
    Nomad::World world{3};
    Nomad::Company company{};
    company.name.clear();
    company.mothership = Nomad::Mothership{Nomad::SystemId::FromIndex(0), Nomad::MothershipState::Damaged, 0, Nomad::ShipClass::Scout, 0};
    world.Companies().Add(company);

    constexpr std::size_t VERSION_BYTES = sizeof(std::uint16_t);
    constexpr std::size_t SEED_BYTES = sizeof(std::uint64_t);
    constexpr std::size_t TICK_BYTES = sizeof(Neuron::Tick);
    constexpr std::size_t TABLE_COUNT_BYTES = sizeof(std::uint32_t);
    constexpr std::size_t EMPTY_STRING_BYTES = sizeof(std::uint32_t);
    constexpr std::size_t ID_BYTES = sizeof(std::uint32_t);
    constexpr std::size_t STATE_OFFSET = VERSION_BYTES + SEED_BYTES + TICK_BYTES + TABLE_COUNT_BYTES + EMPTY_STRING_BYTES + ID_BYTES;

    std::vector<std::byte> bytes = Saved(world);
    Assert::IsTrue(bytes.size() > STATE_OFFSET, L"the store is shorter than the offset this test computes");
    Assert::AreEqual(static_cast<std::uint8_t>(Nomad::MothershipState::Damaged), static_cast<std::uint8_t>(bytes[STATE_OFFSET]),
                     L"the mothership state is not where this test thinks it is; the layout changed");

    bytes[STATE_OFFSET] = static_cast<std::byte>(200);
    Nomad::World loaded{4};
    Neuron::ByteReader reader{bytes};
    Assert::IsFalse(loaded.Deserialize(reader), L"a mothership state of 200 was accepted into the world");
  }

  TEST_METHOD(TheStreamsAreForkedFromTheSeedAndAreIndependent)
  {
    // R16, and the reason Fork exists: adding a consumer later must not shift what the existing ones draw. Two worlds
    // from one seed agree; drawing from one stream does not move another.
    Nomad::World left{0xABCDEFu};
    Nomad::World right{0xABCDEFu};
    Assert::AreEqual(left.Hash(), right.Hash(), L"two worlds from one seed differ");

    const std::uint32_t firstBattle = left.RandomFor(Nomad::RandomStream::Battle).Next();

    // Drawing from Detection on the right must not disturb Battle there.
    for (int draw = 0; draw < 10; ++draw)
    {
      (void)right.RandomFor(Nomad::RandomStream::Detection).Next();
    }
    Assert::AreEqual(firstBattle, right.RandomFor(Nomad::RandomStream::Battle).Next(),
                     L"drawing from one stream shifted another, so adding a consumer would break every older store");

    // And two streams of one world are not the same sequence, which is what makes them worth having. The two draws
    // are sequenced into locals on purpose: the order in which C++ evaluates two function arguments is unspecified,
    // and a test of a generator must not depend on it.
    Nomad::World world{7};
    const std::uint32_t economy = world.RandomFor(Nomad::RandomStream::Economy).Next();
    const std::uint32_t empires = world.RandomFor(Nomad::RandomStream::Empires).Next();
    Assert::AreNotEqual(economy, empires);
  }
};

} // namespace GameLogicTests
