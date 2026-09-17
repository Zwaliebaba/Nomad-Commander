// Tests/GameLogicTests/TickResolverTests.cpp
#include "pch.h"
#include "TickResolver.h"
#include "Tuning.h"

#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

[[nodiscard]] Nomad::CompanyId AddCompany(Nomad::World& _world)
{
  Nomad::Company company{};
  company.name = "Sedu Compact";
  company.activeWindow =
    Nomad::ActiveWindow{Nomad::Tuning::DEFAULT_ACTIVE_WINDOW_START_TICK_OF_DAY, Nomad::Tuning::DEFAULT_ACTIVE_WINDOW_LENGTH_TICKS, 0};
  company.alive = true;
  return _world.Companies().Add(company);
}

/// A foothold to aim a governor's orders at. The resolver property two tests below needs an input with **no rule of
/// its own**: two decisions on one tick have to both apply, and GDD §7 will not let the active window move twice in a
/// day (NC-066). A governor's policy has no such clock, so it is the honest thing to test ordering with.
[[nodiscard]] Nomad::OutpostId AddOutpost(Nomad::World& _world, Nomad::CompanyId _company)
{
  Nomad::Outpost outpost{};
  outpost.name = "Harrow Depot";
  outpost.owningCompany = _company;
  outpost.stockByGood.assign(Nomad::GOOD_COUNT, 0);
  outpost.alive = true;
  const Nomad::OutpostId id = _world.Outposts().Add(outpost);
  _world.Companies().Get(_company).outposts.push_back(id);
  return id;
}

[[nodiscard]] Nomad::Input Reserve(Neuron::Tick _applyAtTick, Nomad::CompanyId _company, Nomad::OutpostId _outpost, std::uint32_t _units)
{
  Nomad::Input input{};
  input.applyAtTick = _applyAtTick;
  input.kind = Nomad::InputKind::SetGovernorPolicy;
  input.company = _company;
  input.outpost = _outpost;
  input.policy.fuelReserveUnits = _units;
  return input;
}

[[nodiscard]] Nomad::Input Window(Neuron::Tick _applyAtTick, Nomad::CompanyId _company, Neuron::Tick _start)
{
  Nomad::Input input{};
  input.applyAtTick = _applyAtTick;
  input.kind = Nomad::InputKind::SetActiveWindow;
  input.company = _company;
  input.activeWindowStartTickOfDay = _start;
  input.activeWindowLengthTicks = Neuron::TICKS_PER_HOUR;
  return input;
}

} // namespace

TEST_CLASS(TickResolverTests)
{
public:
  TEST_METHOD(OneAdvanceIsExactlyOneTick)
  {
    // The tick is the clock (R21). A resolver that advanced by two on a day boundary, or not at all when there was
    // nothing to do, would make every timer in the game wrong in a way no single system would own.
    Nomad::World world{1};
    std::vector<Nomad::Event> events;
    Nomad::Knowledge knowledge;
    for (Neuron::Tick expected = 1; expected <= 3 * Neuron::TICKS_PER_DAY; ++expected)
    {
      Nomad::TickResolver::Advance(world, knowledge, {}, events);
      Assert::AreEqual(expected, world.CurrentTick(), L"a tick did not advance by exactly one");
    }
  }

  TEST_METHOD(AnInputAppliesOnItsTickAndIsIgnoredOnEveryOther)
  {
    Nomad::World world{2};
    const Nomad::CompanyId company = AddCompany(world);
    // **A day and a tick apart.** GDD §7 puts a one-day cooldown on moving the active window, and NC-066 made that a
    // rule of the simulation rather than a note; two changes in an afternoon is exactly what it refuses. What this
    // test is about is *when* an input applies, so the two it schedules have to be two the simulation would accept.
    constexpr Neuron::Tick FIRST_AT = 3;
    constexpr Neuron::Tick SECOND_AT = FIRST_AT + Neuron::TICKS_PER_DAY + 1;
    const Nomad::Input inputs[] = {Window(FIRST_AT, company, 100), Window(SECOND_AT, company, 200)};

    std::vector<Nomad::Event> events;
    Nomad::Knowledge knowledge;
    for (Neuron::Tick tick = 1; tick <= SECOND_AT + 3; ++tick)
    {
      Nomad::TickResolver::Advance(world, knowledge, inputs, events);
      const Neuron::Tick start = world.Companies().Get(company).activeWindow.startTickOfDay;
      if (tick < FIRST_AT)
      {
        Assert::AreEqual(Nomad::Tuning::DEFAULT_ACTIVE_WINDOW_START_TICK_OF_DAY, start, L"an input applied before its tick");
      }
      else if (tick < SECOND_AT)
      {
        Assert::AreEqual(Neuron::Tick{100}, start, L"the first input did not hold until the second");
      }
      else
      {
        Assert::AreEqual(Neuron::Tick{200}, start, L"the second input did not apply");
      }
    }
    Assert::AreEqual(std::size_t{2}, events.size(), L"two decisions produced something other than two events");
  }

  TEST_METHOD(EveryEventCarriesTheTickItHappenedOnAndAReason)
  {
    // R19 at the resolver rather than at the type: the events that actually come out have to carry a reason, not
    // merely be capable of carrying one.
    Nomad::World world{3};
    const Nomad::CompanyId company = AddCompany(world);
    const Nomad::OutpostId outpost = AddOutpost(world, company);
    const Nomad::Input inputs[] = {Reserve(2, company, outpost, 10), Reserve(4, company, outpost, 20), Reserve(4, company, outpost, 30)};

    std::vector<Nomad::Event> events;
    Nomad::Knowledge knowledge;
    for (Neuron::Tick tick = 1; tick <= 5; ++tick)
    {
      Nomad::TickResolver::Advance(world, knowledge, inputs, events);
    }

    Assert::AreEqual(std::size_t{3}, events.size());
    Assert::AreEqual(Neuron::Tick{2}, events[0].tick);
    Assert::AreEqual(Neuron::Tick{4}, events[1].tick);
    Assert::AreEqual(Neuron::Tick{4}, events[2].tick);
    for (const Nomad::Event& event : events)
    {
      Assert::IsTrue(event.explanation.reason != Nomad::ReasonCode::Unknown, L"an event came out with no reason");
      Assert::IsTrue(event.subjects.company == company);
    }

    // Two inputs on one tick apply in the order they were given, which is the order the receipt will explain them in
    // (GDD §4). The last one wins, and the events record both.
    Assert::AreEqual(30u, world.Outposts().Get(outpost).policy.fuelReserveUnits);
  }

  TEST_METHOD(AnInputNamingNothingIsSteppedOverRatherThanCrashing)
  {
    // NomadSimulation refuses these at the seam, so the resolver should never see one. It is defended anyway: a
    // scenario or a restored journal is a second way inputs arrive, and an assert in a headless year-long run is a
    // worse outcome than a decision quietly having no target.
    Nomad::World world{4};
    const Nomad::Input inputs[] = {Window(1, Nomad::CompanyId::FromIndex(9), 60)};
    std::vector<Nomad::Event> events;
    Nomad::Knowledge knowledge;
    Nomad::TickResolver::Advance(world, knowledge, inputs, events);
    Assert::AreEqual(std::size_t{0}, events.size(), L"an input naming no company produced an event");
    Assert::AreEqual(Neuron::Tick{1}, world.CurrentTick());
  }

  TEST_METHOD(TwoWorldsFedTheSameInputsAgreeTickForTick)
  {
    // The determinism property at the resolver's own level, before NC-043 builds the harness that runs it in every
    // later PR. The hash is compared every tick rather than at the end, so a divergence names the tick it began on.
    Nomad::World left{0xC0FFEE};
    Nomad::World right{0xC0FFEE};
    const Nomad::CompanyId leftCompany = AddCompany(left);
    const Nomad::CompanyId rightCompany = AddCompany(right);
    Assert::IsTrue(leftCompany == rightCompany);

    // **One change a day, which is GDD §7's cooldown** (NC-066). Three decisions therefore need three days, and the
    // extra day is a day more of the daily systems drawing from the PRNG in both runs -- which is what this test is
    // for.
    const Nomad::Input inputs[] = {Window(2, leftCompany, 60), Window(Neuron::TICKS_PER_DAY + 3, leftCompany, 900),
                                   Window(2 * Neuron::TICKS_PER_DAY + 4, leftCompany, 300)};

    std::vector<Nomad::Event> leftEvents;
    std::vector<Nomad::Event> rightEvents;
    Nomad::Knowledge leftKnowledge;
    Nomad::Knowledge rightKnowledge;
    for (Neuron::Tick tick = 1; tick <= 3 * Neuron::TICKS_PER_DAY; ++tick)
    {
      Nomad::TickResolver::Advance(left, leftKnowledge, inputs, leftEvents);
      Nomad::TickResolver::Advance(right, rightKnowledge, inputs, rightEvents);
      Assert::AreEqual(left.Hash(), right.Hash(), (L"the two worlds diverged at tick " + std::to_wstring(tick)).c_str());
      // **Both halves, since NC-051.** Reports and beliefs left `World` for `Knowledge`, and a comparison of the
      // world alone would no longer see a detection phase that drew from the PRNG differently in two runs -- which
      // is exactly the defect NC-050 shipped and R16 caught (`SensorTests`, NC-050's report).
      Assert::AreEqual(leftKnowledge.Hash(), rightKnowledge.Hash(),
                       (L"the two knowledge halves diverged at tick " + std::to_wstring(tick)).c_str());
    }
    Assert::AreEqual(leftEvents.size(), rightEvents.size());
    Assert::AreEqual(std::size_t{3}, leftEvents.size(), L"the three decisions did not all fire across three days");
  }
};

} // namespace GameLogicTests
