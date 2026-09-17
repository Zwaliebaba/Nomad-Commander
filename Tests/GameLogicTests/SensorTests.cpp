// Tests/GameLogicTests/SensorTests.cpp
#include "pch.h"
#include "Mobility.h"
#include "Politics.h"
#include "Sensor.h"
#include "TickResolver.h"
#include "Tuning.h"
#include "UniverseGenerator.h"
#include "WireReport.h"

#include "ByteReader.h"
#include "ByteWriter.h"

#include <string>
#include <variant>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

constexpr std::uint32_t SYSTEMS = 10;
constexpr std::uint32_t EMPIRES = 3;

[[nodiscard]] Nomad::World Generated(std::uint64_t _seed)
{
  Nomad::World world{_seed};
  const Nomad::UniverseGenerator::Desc desc{SYSTEMS, EMPIRES};
  Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, world), L"the world could not be generated");
  return world;
}

/// A company with a mothership, so that an observer has a desk for its post to reach (GDD §4).
[[nodiscard]] Nomad::CompanyId AddCompany(Nomad::World& _world, Nomad::SystemId _at)
{
  Nomad::Company company{};
  company.name = "Sedu Compact";
  company.mothership =
    Nomad::Mothership{_at, Nomad::MothershipState::Healthy, Nomad::Tuning::MOTHERSHIP_RESERVE_FUEL, Nomad::ShipClass::Scout, 0};
  company.treasury = 1000;
  company.alive = true;
  return _world.Companies().Add(company);
}

/// A fleet of one class, parked, alive and fuelled.
[[nodiscard]] Nomad::FleetId AddFleet(Nomad::World& _world, Nomad::FleetOwner _owner, Nomad::ShipClass _shipClass, std::uint32_t _hulls,
                                      Nomad::SystemId _at)
{
  Nomad::Fleet fleet{};
  fleet.name = "Fleet";
  fleet.owner = _owner;
  fleet.role = Nomad::FleetRole::Operational;
  fleet.ships.Add(_shipClass, _hulls);
  fleet.position = Nomad::AtSystem{_at};
  fleet.cargoByGood.assign(Nomad::GOOD_COUNT, 0);
  fleet.alive = true;
  const Nomad::FleetId id = _world.Fleets().Add(fleet);
  _world.Fleets().Get(id).fuel = Nomad::Mobility::FuelCapacity(_world.Fleets().Get(id));
  return id;
}

/// Finds a system exactly `_jumps` from `_from`, or an invalid id when the map has none.
[[nodiscard]] Nomad::SystemId SystemAtDistance(const Nomad::World& _world, Nomad::SystemId _from, std::uint32_t _jumps)
{
  for (std::uint32_t index = 0; index < _world.Systems().Count(); ++index)
  {
    const auto candidate = Nomad::SystemId::FromIndex(index);
    if (_world.JumpsBetween(_from, candidate) == _jumps)
    {
      return candidate;
    }
  }
  return Nomad::SystemId{};
}

/// Runs one tick through the whole resolver, which is the only way detection ever runs.
void Tick(Nomad::World& _world, std::vector<Nomad::Event>& _events)
{
  Nomad::TickResolver::Advance(_world, {}, _events);
}

/// Makes a fleet look as though it just arrived, so that the movement phase writes the event detection reads. A
/// one-lane route out of where it stands and back is the shortest honest way to produce an arrival.
void SendOneLane(Nomad::World& _world, Nomad::FleetId _fleet)
{
  Nomad::Fleet& fleet = _world.Fleets().Get(_fleet);
  const Nomad::SystemId at = Nomad::Mobility::LocationOf(fleet);
  const Nomad::StarSystem& system = _world.Systems().Get(at);
  Assert::IsTrue(!system.lanes.empty(), L"the generated map left a system with no lanes");
  fleet.route = {system.lanes.front()};
}

} // namespace

/// The boundary between reality and everything anyone knows (GDD §4, §9, R18).
TEST_CLASS(SensorTests)
{
public:
  TEST_METHOD(ReliabilityIsTheTrackRecordAndNotTheTruth)
  {
    // **The rule GDD §4 states and the one most easily broken by accident**: "the reliability shown is the source's
    // track record, never the game's own knowledge of the truth." So the test builds a source that has been telling
    // the truth and marks its record badly anyway, and the reliability follows the record.
    Nomad::SourceRecord unproven{0, 0};
    Assert::AreEqual(Nomad::UNPROVEN_RELIABILITY_HUNDREDTHS, unproven.Reliability().Raw(),
                     L"a source nobody has checked does not read as certain, and does not read as wrong either");

    const Nomad::SourceRecord truthfulButDoubted{1, 9};
    Assert::AreEqual(10, truthfulButDoubted.Reliability().Raw(), L"reliability is confirmed over checked and nothing else");

    const Nomad::SourceRecord luckyLiar{9, 1};
    Assert::AreEqual(90, luckyLiar.Reliability().Raw(), L"a source with a good record reads well however wrong it happens to be");

    // And the function that answers it takes no subject and no truth to consult: it is not callable with a fleet.
    static_assert(!std::is_invocable_v<decltype(&Nomad::SourceRecord::Reliability), const Nomad::SourceRecord&, Nomad::FleetId>,
                  "reliability must not be answerable from anything but the record");
  }

  TEST_METHOD(ASightingCarriesItsAgeFromWhenItWasObserved)
  {
    // GDD §3's sighting is "nine hours old", and that is measured from the observation and never from the delivery:
    // a report that took six hours to arrive is still nine hours old, which is the number that should worry a reader.
    Nomad::Report report{};
    report.observedAtTick = 100;
    report.deliveredAtTick = 100 + 6 * Neuron::TICKS_PER_HOUR;

    const Neuron::Tick nineHoursLater = 100 + 9 * Neuron::TICKS_PER_HOUR;
    Assert::AreEqual(9 * Neuron::TICKS_PER_HOUR, Nomad::AgeTicks(report, nineHoursLater), L"age is measured from the observation");
    Assert::IsTrue(Nomad::IsDelivered(report, nineHoursLater), L"six hours of carriage had passed");
    Assert::IsFalse(Nomad::IsDelivered(report, 100 + Neuron::TICKS_PER_HOUR), L"a report still in the post has reached nobody");

    // The client is told the same two ticks and computes the same age, because the answer changes every tick.
    const Nomad::WireReport wire = ToWire(report);
    Assert::AreEqual(9 * Neuron::TICKS_PER_HOUR, Nomad::AgeTicks(wire, nineHoursLater), L"the client's age is the simulation's");
  }

  TEST_METHOD(AFleetInAnotherSystemIsSeenWithoutItsIdentity)
  {
    // GDD §6: identity only when the fleet is marked or shares the observer's system. A picket a jump away counts
    // hulls; it does not read a flag that is not being flown.
    Nomad::World world = Generated(21);
    const auto home = Nomad::SystemId::FromIndex(0);
    const Nomad::SystemId oneJump = SystemAtDistance(world, home, 1);
    Assert::IsTrue(oneJump.IsValid(), L"the generated map has no neighbour of system 0");

    const Nomad::CompanyId company = AddCompany(world, home);
    // A scout sees three jumps (Tuning::SHIP_CLASSES), so one jump is comfortably inside its range.
    (void)AddFleet(world, Nomad::FleetOwner{company}, Nomad::ShipClass::Scout, 1, home);

    const auto empire = Nomad::EmpireId::FromIndex(0);
    const Nomad::FleetId subject = AddFleet(world, Nomad::FleetOwner{empire}, Nomad::ShipClass::Warship, 4, oneJump);
    SendOneLane(world, subject);

    std::vector<Nomad::Event> events;
    for (std::uint32_t attempt = 0; attempt < 8 * Neuron::TICKS_PER_HOUR && world.Reports().Count() == 0; ++attempt)
    {
      Tick(world, events);
    }
    Assert::IsTrue(world.Reports().Count() > 0, L"a fleet departing one jump from a scout produced no report at all");

    bool sawTheSubject = false;
    for (const Nomad::Report& report : world.Reports().Rows())
    {
      const auto* observer = std::get_if<Nomad::CompanyId>(&report.observer);
      if (observer == nullptr || *observer != company || report.sighting.subject != subject)
      {
        continue;
      }
      sawTheSubject = true;
      Assert::IsFalse(report.sighting.identityKnown,
                      L"an unmarked fleet in another system was reported with its identity, which is the fog leaking");
      Assert::IsFalse(report.sighting.marked, L"the fleet was not marked, so the sighting must not say it was");
    }
    Assert::IsTrue(sawTheSubject, L"the company's scout filed no report about the fleet a jump away");
  }

  TEST_METHOD(ACompanyIsToldNothingNoSensorOfItsOwnCouldSee)
  {
    // The acceptance criterion, and the structural half of R18: a report exists only where a hull that could have
    // written it was. A company with no fleets at all learns nothing, however much happens.
    Nomad::World world = Generated(22);
    const auto home = Nomad::SystemId::FromIndex(0);
    const Nomad::CompanyId blind = AddCompany(world, home);

    // Something well outside anything: the furthest system the map has from home.
    Nomad::SystemId far{};
    std::uint32_t furthest = 0;
    for (std::uint32_t index = 0; index < world.Systems().Count(); ++index)
    {
      const auto candidate = Nomad::SystemId::FromIndex(index);
      const std::uint32_t jumps = world.JumpsBetween(home, candidate);
      if (jumps != Nomad::World::UNREACHABLE && jumps > furthest)
      {
        furthest = jumps;
        far = candidate;
      }
    }
    Assert::IsTrue(far.IsValid() && furthest > 3, L"the generated map is too small for anything to be out of range");

    const auto empire = Nomad::EmpireId::FromIndex(0);
    const Nomad::FleetId subject = AddFleet(world, Nomad::FleetOwner{empire}, Nomad::ShipClass::Raider, 2, far);
    SendOneLane(world, subject);

    std::vector<Nomad::Event> events;
    for (std::uint32_t attempt = 0; attempt < 8 * Neuron::TICKS_PER_HOUR; ++attempt)
    {
      Tick(world, events);
    }

    for (const Nomad::Report& report : world.Reports().Rows())
    {
      const auto* observer = std::get_if<Nomad::CompanyId>(&report.observer);
      Assert::IsTrue(observer == nullptr || *observer != blind,
                     L"a company with no hulls at all received a report, so reports are appearing without observers");
    }
  }

  TEST_METHOD(ADistantSightingIsNoisyAndACloseOneIsNot)
  {
    // GDD §4: the player's advantage is interpretation, not information. A sighting from the next system is a
    // reading; one from the observer's own system is a count.
    Nomad::World world = Generated(23);
    const auto home = Nomad::SystemId::FromIndex(0);
    const Nomad::SystemId oneJump = SystemAtDistance(world, home, 1);
    Assert::IsTrue(oneJump.IsValid());

    const Nomad::CompanyId company = AddCompany(world, home);
    (void)AddFleet(world, Nomad::FleetOwner{company}, Nomad::ShipClass::Scout, 1, home);

    constexpr std::uint32_t TRUE_HULLS = 8;
    const auto empire = Nomad::EmpireId::FromIndex(0);
    const Nomad::FleetId nearby = AddFleet(world, Nomad::FleetOwner{empire}, Nomad::ShipClass::Warship, TRUE_HULLS, home);
    const Nomad::FleetId distant = AddFleet(world, Nomad::FleetOwner{empire}, Nomad::ShipClass::Warship, TRUE_HULLS, oneJump);
    SendOneLane(world, nearby);
    SendOneLane(world, distant);

    std::vector<Nomad::Event> events;
    for (std::uint32_t attempt = 0; attempt < 8 * Neuron::TICKS_PER_HOUR; ++attempt)
    {
      Tick(world, events);
    }

    const auto warship = static_cast<std::uint32_t>(Nomad::ShipClass::Warship);
    bool sawNear = false;
    for (const Nomad::Report& report : world.Reports().Rows())
    {
      const auto* observer = std::get_if<Nomad::CompanyId>(&report.observer);
      if (observer == nullptr || *observer != company || report.sighting.subject != nearby)
      {
        continue;
      }
      // **Only the sighting made in the observer's own system.** The same fleet is seen again a jump away once it
      // has crossed, and that report is legitimately noisy -- asserting exactness over every report about it would
      // be asserting that distance costs nothing.
      if (report.sighting.atSystem != home)
      {
        continue;
      }
      sawNear = true;
      Assert::AreEqual(TRUE_HULLS, report.sighting.countsSeen.byClass[warship],
                       L"a fleet in the observer's own system was counted wrongly, so the noise is not scaled by distance");
      Assert::IsTrue(report.sighting.identityKnown, L"a fleet sharing the observer's system is identified (GDD §6)");
    }
    Assert::IsTrue(sawNear, L"no report was filed about the fleet in the observer's own system");

    // And the one that was only ever seen from a jump away is a reading rather than a count: over the whole run at
    // least one of its sightings differs from the truth, which is what SIGHTING_NOISE_HUNDREDTHS_PER_JUMP buys.
    bool sawDistant = false;
    bool anyNoise = false;
    for (const Nomad::Report& report : world.Reports().Rows())
    {
      const auto* observer = std::get_if<Nomad::CompanyId>(&report.observer);
      if (observer == nullptr || *observer != company || report.sighting.subject != distant || report.sighting.atSystem == home)
      {
        continue;
      }
      sawDistant = true;
      anyNoise = anyNoise || report.sighting.countsSeen.byClass[warship] != TRUE_HULLS;
    }
    Assert::IsTrue(sawDistant, L"the scout filed nothing about the fleet a jump away");
    Assert::IsTrue(anyNoise, L"every sighting from a jump away was exactly right, so distance costs nothing");
  }

  TEST_METHOD(ACloserSightingChecksTheOlderOneAndMovesTheRecord)
  {
    // GDD §4's track record is made of moments like this: a sighting from a distance guessed a number, a later
    // contact revealed the real one, and the source's record carries the difference from then on. The check is on
    // **counts**, not position -- a fleet that moved is not a source that lied.
    Nomad::World world = Generated(24);
    const auto home = Nomad::SystemId::FromIndex(0);
    const Nomad::CompanyId company = AddCompany(world, home);
    const auto observer = Nomad::Observer{company};

    Assert::AreEqual(Nomad::UNPROVEN_RELIABILITY_HUNDREDTHS,
                     Nomad::Sensor::ReliabilityOf(world, observer, Nomad::ReportSource::Picket).Raw(),
                     L"an observer starts with no record of any source");

    // A report that got it wrong, and the contradiction that follows.
    Nomad::Report wrong{};
    wrong.observedAtTick = world.CurrentTick();
    wrong.deliveredAtTick = world.CurrentTick();
    wrong.source = Nomad::ReportSource::Picket;
    wrong.observer = observer;
    wrong.sighting.subject = Nomad::FleetId::FromIndex(0);
    const Nomad::ReportId wrongId = world.Reports().Add(wrong);
    Nomad::Sensor::RecordOutcome(world, wrongId, false);

    Assert::AreEqual(0, Nomad::Sensor::ReliabilityOf(world, observer, Nomad::ReportSource::Picket).Raw(),
                     L"one contradiction and no confirmations is a record of nothing right");
    Assert::IsTrue(world.Reports().Get(wrongId).checked, L"a checked report is marked, so one bad guess costs its source once");

    // Checking it twice does not cost twice.
    Nomad::Sensor::RecordOutcome(world, wrongId, false);
    Assert::AreEqual(1u,
                     world.Companies().Get(company).recordBySource[static_cast<std::uint32_t>(Nomad::ReportSource::Picket)].contradicted,
                     L"a report was checked twice and its source paid twice");

    // A confirmation moves it back toward the middle, and only the record decides.
    const Nomad::ReportId rightId = world.Reports().Add(wrong);
    Nomad::Sensor::RecordOutcome(world, rightId, true);
    Assert::AreEqual(50, Nomad::Sensor::ReliabilityOf(world, observer, Nomad::ReportSource::Picket).Raw(),
                     L"one right and one wrong is half, computed from the record alone");
  }

  TEST_METHOD(AnEmpireBelievesWhatItWasToldAndNotWhatIsThere)
  {
    // R18 made observable: `BelievedSituation` counts foreign hulls from delivered reports, so an empire that has
    // looked at nothing believes nothing is out there however much is.
    Nomad::World world = Generated(25);
    const auto empire = Nomad::EmpireId::FromIndex(0);

    const Nomad::BelievedSituation blind = Nomad::Politics::Believe(world, empire);
    Assert::AreEqual(0u, blind.reportsRead, L"a freshly generated empire has been told nothing");
    Assert::AreEqual(0u, blind.sightedForeignHulls, L"an empire that has read nothing believes it has seen nothing");

    // One delivered report, and the belief moves by exactly what the report said -- not by what the fleet holds.
    Nomad::Report told{};
    told.observedAtTick = world.CurrentTick();
    told.deliveredAtTick = world.CurrentTick();
    told.source = Nomad::ReportSource::Scout;
    told.observer = Nomad::Observer{empire};
    told.sighting.subject = Nomad::FleetId::FromIndex(0);
    told.sighting.countsSeen.Add(Nomad::ShipClass::Warship, 5);
    (void)world.Reports().Add(told);

    // And one that has not arrived yet, which must not count for anything.
    Nomad::Report inThePost = told;
    inThePost.deliveredAtTick = world.CurrentTick() + Neuron::TICKS_PER_DAY;
    inThePost.sighting.countsSeen.Add(Nomad::ShipClass::Warship, 100);
    (void)world.Reports().Add(inThePost);

    const Nomad::BelievedSituation informed = Nomad::Politics::Believe(world, empire);
    Assert::AreEqual(1u, informed.reportsRead, L"a report still in a courier's hold was counted as read");
    Assert::AreEqual(5u, informed.sightedForeignHulls, L"the belief is the report's counts and nothing else");
  }

  TEST_METHOD(AReportAndATrackRecordSurviveTheStore)
  {
    // `WorldTests::EveryFieldReachesTheStore` proves a field reaches the bytes by mutating it and watching the hash;
    // the two things NC-050 added to the world are checked the same way here, beside the code that writes them.
    Nomad::World world = Generated(26);
    const auto home = Nomad::SystemId::FromIndex(0);
    const Nomad::CompanyId company = AddCompany(world, home);

    Nomad::Report report{};
    report.observedAtTick = 4321;
    report.deliveredAtTick = 5678;
    report.source = Nomad::ReportSource::Scout;
    report.observer = Nomad::Observer{company};
    report.sighting.subject = Nomad::FleetId::FromIndex(0);
    report.sighting.countsSeen.Add(Nomad::ShipClass::Raider, 3);
    report.sighting.atSystem = home;
    report.sighting.identityKnown = true;
    report.sighting.marked = true;
    report.sighting.inTransit = true;
    report.reliabilityWhenWritten = Neuron::Hundredths::FromRaw(37);
    const Nomad::ReportId reportId = world.Reports().Add(report);

    const std::uint64_t withReport = world.Hash();

    // Every field the report carries moves the hash, so none of them was forgotten by Serialize.
    Nomad::World mutated = world;
    mutated.Reports().Get(reportId).observedAtTick += 1;
    Assert::AreNotEqual(withReport, mutated.Hash(), L"a report's observation tick does not reach the store");

    mutated = world;
    mutated.Reports().Get(reportId).sighting.identityKnown = false;
    Assert::AreNotEqual(withReport, mutated.Hash(), L"whether an identity was known does not reach the store");

    mutated = world;
    mutated.Reports().Get(reportId).reliabilityWhenWritten = Neuron::Hundredths::FromRaw(38);
    Assert::AreNotEqual(withReport, mutated.Hash(), L"the reliability a report was written with does not reach the store");

    mutated = world;
    mutated.Companies().Get(company).recordBySource[static_cast<std::uint32_t>(Nomad::ReportSource::Scout)].confirmed += 1;
    Assert::AreNotEqual(withReport, mutated.Hash(), L"an observer's track record does not reach the store");

    // And the whole thing comes back as it went in, the observer's variant arm included.
    Neuron::ByteWriter writer;
    world.Serialize(writer);
    Nomad::World restored{0};
    Neuron::ByteReader reader{writer.Bytes()};
    Assert::IsTrue(restored.Deserialize(reader), L"a world holding a report could not be read back");
    Assert::AreEqual(withReport, restored.Hash(), L"a world holding a report did not survive its own store");

    const Nomad::Report& back = restored.Reports().Get(reportId);
    Assert::IsTrue(std::holds_alternative<Nomad::CompanyId>(back.observer), L"the observer came back as the wrong kind");
    Assert::IsTrue(std::get<Nomad::CompanyId>(back.observer) == company);
    Assert::AreEqual(3u, back.sighting.countsSeen.Of(Nomad::ShipClass::Raider));
    Assert::AreEqual(37, back.reliabilityWhenWritten.Raw());
  }

  TEST_METHOD(ADecisionStillCannotBeMadeFromTheWorld)
  {
    // NC-047 asserted this of `ChooseAnEnemy` and NC-050 is the task that gave the type something to be wrong about,
    // so it is worth asserting again now that beliefs carry foreign strength: the routine that acts on it still
    // cannot be handed the truth instead.
    static_assert(!std::is_invocable_v<decltype(&Nomad::Politics::ChooseAnEnemy), const Nomad::World&>,
                  "an empire's decision must not be callable with a World");
    static_assert(std::is_invocable_v<decltype(&Nomad::Politics::ChooseAnEnemy), const Nomad::BelievedSituation&>);
  }
};

} // namespace GameLogicTests
