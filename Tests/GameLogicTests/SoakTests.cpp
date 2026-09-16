// Tests/GameLogicTests/SoakTests.cpp
#include "pch.h"
#include "NomadSimulation.h"
#include "Politics.h"
#include "Tuning.h"
#include "UniverseGenerator.h"

#include "ByteReader.h"
#include "ByteWriter.h"

#include <chrono>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

/// One simulated year at one tick a simulated minute (ADR-005): 525,600 ticks.
constexpr std::uint32_t YEAR_DAYS = 365;
constexpr Neuron::Tick YEAR_TICKS = Neuron::Tick{YEAR_DAYS} * Neuron::TICKS_PER_DAY;

/// The day a store is written on, deliberately not a day the daily phase has just run and not half way either: the
/// reload has to land correctly whatever the phase order was doing.
constexpr std::uint32_t STORE_AT_DAY = 180;

/// GDD §15's v0.1 scope: "three empires and about ten systems".
constexpr std::uint64_t SOAK_SEED = 0x50A4;
constexpr std::uint32_t SOAK_SYSTEMS = 10;
constexpr std::uint32_t SOAK_EMPIRES = 3;

/// **What a map may be short of, and why this seed.** Every generated map runs a per-good deficit -- see
/// `OneYearStocksStayBounded` for the arithmetic and NC-049 for the fix. Nine in ten maps are short one unit a day of
/// three goods, which a year absorbs; the other one in ten is short five units a day of one good, which it does not.
/// `SOAK_SEED` is one of the first kind, and picking it was a choice rather than a default: a soak that ran on a map
/// whose economy is collapsing would be measuring the collapse.
constexpr std::uint32_t MAX_DRY_DAYS = 14;

/// The map-wide stock may fall well below what it started with -- goods sit in convoys, a glut is clipped at the
/// capacity, and the per-good deficit above drains a little every day -- but it may not collapse. Measured over this
/// year: it bottoms out at 85 percent of the start.
constexpr std::uint32_t STOCK_FLOOR_PERCENT_OF_START = 70;

/// **The floor CI must clear, and it is deliberately an order of magnitude below the measurement.**
///
/// Measured on the machine that wrote this task -- an Intel Xeon at 2.10 GHz, 4 vCPU, Ubuntu 24.04 -- with clang 18
/// at `-O0 -D_DEBUG`, which is the nearest thing that machine has to `Debug|x64`: a year is **1.56 seconds, about
/// 338,000 ticks a second**, over five runs spanning 1.51 to 1.94 s. The same year at `-O2 -DNDEBUG` is 0.098
/// seconds, about 5.3 million. The MSVC figures are the CI runner's and this task's report carries them.
///
/// A year at the floor below would take 105 seconds, which still fits the job. A tight budget on a shared runner is a
/// flaky test rather than a useful one; the number worth catching here is an order of magnitude.
constexpr double MINIMUM_TICKS_PER_SECOND = 5000.0;

/// A generated three-empire world with nothing of the player's in it. **No company and no inputs**, which is the
/// point: this measures the world running on its own, the way it does while the player is away (GDD §1, §7).
void Generate(Nomad::NomadSimulation& _simulation)
{
  const Nomad::UniverseGenerator::Desc desc{SOAK_SYSTEMS, SOAK_EMPIRES};
  Assert::IsTrue(Nomad::UniverseGenerator::Generate(desc, _simulation.MutableWorld()), L"the soak's map could not be generated");
}

void RunTicks(Nomad::NomadSimulation& _simulation, Neuron::Tick _ticks)
{
  // Events are not drained. A year produces 1,662 of them in 85 KiB, measured, so draining changes neither the state
  // hash nor the cost by anything worth the extra moving part -- and NC-043's month is timed the same way, which is
  // what makes the two figures comparable.
  for (Neuron::Tick tick = 0; tick < _ticks; ++tick)
  {
    _simulation.Advance();
  }
}

void RunDays(Nomad::NomadSimulation& _simulation, std::uint32_t _days)
{
  RunTicks(_simulation, Neuron::Tick{_days} * Neuron::TICKS_PER_DAY);
}

[[nodiscard]] std::uint32_t MapStockTotal(const Nomad::World& _world)
{
  std::uint32_t total = 0;
  for (std::uint32_t index = 0; index < _world.Markets().Count(); ++index)
  {
    total += _world.Markets().Get(Nomad::SystemId::FromIndex(index)).stock.Total();
  }
  return total;
}

} // namespace

/// **Phase 2's exit criterion, as a test that stays in the suite.**
///
/// A generated three-empire world runs one simulated year with nobody at the desk: twice to the same hash, restoring
/// from a store written mid-year into the same future, with the economy bounded, a war somewhere on every day, and
/// the whole year inside a stated time budget.
///
/// A failure here after a later task is **that task's**, the same way `DeterminismTests` is. The difference is the
/// length: thirty days catch a rule that is wrong, a year catches a rule that leaks -- a quantity that drifts, a
/// table that grows, a guarantee that holds for a month because nothing has had time to break it yet.
TEST_CLASS(SoakTests)
{
public:
  TEST_METHOD(OneYearIsDeterministic)
  {
    // R16 over 525,600 ticks rather than 43,200. One float in GameLogic, one unordered container iterated into the
    // world, one wall-clock read, and the replay is gone -- and the longer the run, the smaller the mistake it takes.
    Nomad::NomadSimulation first{SOAK_SEED};
    Generate(first);
    RunTicks(first, YEAR_TICKS);

    Nomad::NomadSimulation second{SOAK_SEED};
    Generate(second);
    RunTicks(second, YEAR_TICKS);

    Assert::AreEqual(first.StateHash(), second.StateHash(), L"two runs of one seed ended a simulated year in different states");
    Assert::AreEqual(YEAR_TICKS, first.CurrentTick(), L"a year did not advance the clock by a year");
  }

  TEST_METHOD(OneYearRestoresFromStoreAtDay180)
  {
    // **This is what loading a universe store is** (ADR-014): there is no snapshot section, so a reload is correct
    // exactly when continuing from it lands where continuing without it would have. A month proved the mechanism;
    // a year is where a quantity that was not serialized has had time to matter.
    Nomad::NomadSimulation lived{SOAK_SEED};
    Generate(lived);
    RunTicks(lived, YEAR_TICKS);

    Nomad::NomadSimulation interrupted{SOAK_SEED};
    Generate(interrupted);
    RunDays(interrupted, STORE_AT_DAY);

    Neuron::ByteWriter writer;
    interrupted.WriteState(writer);

    Nomad::NomadSimulation restored{0};
    Neuron::ByteReader reader{writer.Bytes()};
    Assert::IsTrue(restored.ReadState(reader), L"the store written at day 180 could not be read back");
    Assert::AreEqual(std::size_t{0}, reader.Remaining(), L"the reader did not consume the whole state");
    Assert::AreEqual(Neuron::Tick{STORE_AT_DAY} * Neuron::TICKS_PER_DAY, restored.CurrentTick(),
                     L"the restored state did not come back on the tick it was written at");

    RunDays(restored, YEAR_DAYS - STORE_AT_DAY);
    Assert::AreEqual(lived.StateHash(), restored.StateHash(), L"a year continued from a store written at day 180 ended somewhere else");
  }

  TEST_METHOD(OneYearStocksStayBounded)
  {
    // GDD §10: a system "produces a fixed flow of the goods its role implies ... and consumes a fixed flow of the
    // others, **so stocks neither run away nor drain to zero**". Three readings of that, taken every day.
    //
    // **The soak found that the third one holds for a year and not for two, on every map the generator makes.** The
    // arithmetic, because it is short: every map is ten systems of which nine are owned and one is the harbour GDD
    // §8's contraction left behind, and a harbour balances its own books (NC-045). An owned system eats
    // CONSUMPTION_PER_DAY of each good and makes BASELINE_PRODUCTION_PER_DAY of each plus ROLE_PRODUCTION_BONUS_PER_DAY
    // of the one its role implies, so the map's daily balance in good g is
    //
    //     9 * 5 + 4 * n(g) - 9 * 6  =  4 * n(g) - 9
    //
    // which is zero only at n(g) = 2.25. **There is no distribution of roles that balances a nine-system map per
    // good**, and the second year is where the difference shows: 1,035 dry market-days against this year's 6. The
    // fix is a decision about where the balancing term lives and it is **NC-049**, not this test.
    Nomad::NomadSimulation simulation{SOAK_SEED};
    Generate(simulation);

    const std::uint32_t startingTotal = MapStockTotal(simulation.CurrentWorld());
    const std::uint32_t floorTotal = startingTotal * STOCK_FLOOR_PERCENT_OF_START / 100;

    const std::uint32_t marketCount = simulation.CurrentWorld().Markets().Count();
    std::vector<std::uint32_t> dryDays(static_cast<std::size_t>(marketCount) * Nomad::GOOD_COUNT, 0);
    std::uint32_t lowestTotal = startingTotal;

    for (std::uint32_t day = 1; day <= YEAR_DAYS; ++day)
    {
      RunDays(simulation, 1);
      const Nomad::World& world = simulation.CurrentWorld();

      const std::uint32_t total = MapStockTotal(world);
      if (total < lowestTotal)
      {
        lowestTotal = total;
      }
      Assert::IsTrue(total >= floorTotal, (L"on day " + std::to_wstring(day) + L" the map held " + std::to_wstring(total) +
                                           L" units against a floor of " + std::to_wstring(floorTotal))
                                            .c_str());

      for (std::uint32_t index = 0; index < marketCount; ++index)
      {
        const Nomad::Market& market = world.Markets().Get(Nomad::SystemId::FromIndex(index));
        for (std::uint32_t good = 0; good < Nomad::GOOD_COUNT; ++good)
        {
          // Nothing runs away: a stock is clipped at the days of its own consumption the warehouse holds.
          const std::uint32_t capacity = market.consumedPerDay.byGood[good] * Nomad::Tuning::STOCK_CAPACITY_DAYS;
          Assert::IsTrue(market.stock.byGood[good] <= capacity,
                         (L"on day " + std::to_wstring(day) + L" system " + std::to_wstring(index) + L" held " +
                          std::to_wstring(market.stock.byGood[good]) + L" of good " + std::to_wstring(good) + L" against a capacity of " +
                          std::to_wstring(capacity))
                           .c_str());

          // And nothing sits empty: a shortage is a situation, a permanently dry market is a sink.
          std::uint32_t& dry = dryDays[static_cast<std::size_t>(index) * Nomad::GOOD_COUNT + good];
          dry = market.stock.byGood[good] == 0 ? dry + 1 : 0;
          Assert::IsTrue(dry <= MAX_DRY_DAYS, (L"system " + std::to_wstring(index) + L" ran out of good " + std::to_wstring(good) +
                                               L" for " + std::to_wstring(dry) + L" days running, ending on day " + std::to_wstring(day))
                                                .c_str());
        }
      }
    }

    Logger::WriteMessage((L"[NC-048] a year of stocks: started " + std::to_wstring(startingTotal) + L" units, bottomed out at " +
                          std::to_wstring(lowestTotal) + L", floor " + std::to_wstring(floorTotal))
                           .c_str());
  }

  TEST_METHOD(OneYearIsNeverQuiet)
  {
    // GDD §7 states this as a rule and not a tendency: "at least one conflict must be active in the region at any
    // time ... A three-empire world at peace is a bug." NC-047 asserts it over a year of `TickResolver::Advance`
    // calls against a bare `World`; this asserts it of the whole simulation, with the economy, upkeep, insolvency
    // and the fabricator all running underneath it, which is the only place their interaction can show up.
    Nomad::NomadSimulation simulation{SOAK_SEED};
    Generate(simulation);

    for (std::uint32_t day = 1; day <= YEAR_DAYS; ++day)
    {
      RunDays(simulation, 1);
      Assert::IsTrue(Nomad::Politics::AnyWarActive(simulation.CurrentWorld()),
                     (L"the region was at peace on day " + std::to_wstring(day) + L" of the soak").c_str());
    }
  }

  TEST_METHOD(OneYearFitsTheBudget)
  {
    // The measurement the rest of the plan was waiting on: ADR-005 left `MAX_TICKS_PER_PUMP` to be set against a real
    // tick cost, and ADR-014 said in as many words that "NC-048's one-year soak is what measures a real tick cost".
    // The figure logged here is that measurement; `MINIMUM_TICKS_PER_SECOND` above carries the machine it was taken
    // on and what the floor is for.
    Nomad::NomadSimulation simulation{SOAK_SEED};
    Generate(simulation);

    const auto started = std::chrono::steady_clock::now();
    RunTicks(simulation, YEAR_TICKS);
    const auto elapsed = std::chrono::steady_clock::now() - started;

    const double seconds = std::chrono::duration<double>(elapsed).count();
    const double ticksPerSecond = seconds > 0.0 ? static_cast<double>(YEAR_TICKS) / seconds : 0.0;
    const std::uint32_t fleetRows = simulation.CurrentWorld().Fleets().Count();

    Logger::WriteMessage((L"[NC-048] " + std::to_wstring(YEAR_TICKS) + L" ticks (" + std::to_wstring(YEAR_DAYS) + L" simulated days) in " +
                          std::to_wstring(seconds) + L" s = " + std::to_wstring(ticksPerSecond) + L" ticks/second; " +
                          std::to_wstring(fleetRows) + L" fleet rows at the end; hash " + std::to_wstring(simulation.StateHash()))
                           .c_str());

    Assert::IsTrue(ticksPerSecond > MINIMUM_TICKS_PER_SECOND,
                   (L"a simulated year took " + std::to_wstring(seconds) + L" s, which is " + std::to_wstring(ticksPerSecond) +
                    L" ticks/second and below the floor of " + std::to_wstring(MINIMUM_TICKS_PER_SECOND))
                     .c_str());
  }
};

} // namespace GameLogicTests
