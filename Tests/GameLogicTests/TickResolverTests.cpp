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
    Nomad::ActiveWindow{Nomad::Tuning::DEFAULT_ACTIVE_WINDOW_START_TICK_OF_DAY, Nomad::Tuning::DEFAULT_ACTIVE_WINDOW_LENGTH_TICKS};
  company.alive = true;
  return _world.Companies().Add(company);
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
    for (Neuron::Tick expected = 1; expected <= 3 * Neuron::TICKS_PER_DAY; ++expected)
    {
      Nomad::TickResolver::Advance(world, {}, events);
      Assert::AreEqual(expected, world.CurrentTick(), L"a tick did not advance by exactly one");
    }
  }

  TEST_METHOD(AnInputAppliesOnItsTickAndIsIgnoredOnEveryOther)
  {
    Nomad::World world{2};
    const Nomad::CompanyId company = AddCompany(world);
    const Nomad::Input inputs[] = {Window(3, company, 100), Window(7, company, 200)};

    std::vector<Nomad::Event> events;
    for (Neuron::Tick tick = 1; tick <= 10; ++tick)
    {
      Nomad::TickResolver::Advance(world, inputs, events);
      const Neuron::Tick start = world.Companies().Get(company).activeWindow.startTickOfDay;
      if (tick < 3)
      {
        Assert::AreEqual(Nomad::Tuning::DEFAULT_ACTIVE_WINDOW_START_TICK_OF_DAY, start, L"an input applied before its tick");
      }
      else if (tick < 7)
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
    const Nomad::Input inputs[] = {Window(2, company, 60), Window(4, company, 120), Window(4, company, 180)};

    std::vector<Nomad::Event> events;
    for (Neuron::Tick tick = 1; tick <= 5; ++tick)
    {
      Nomad::TickResolver::Advance(world, inputs, events);
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
    Assert::AreEqual(Neuron::Tick{180}, world.Companies().Get(company).activeWindow.startTickOfDay);
  }

  TEST_METHOD(AnInputNamingNothingIsSteppedOverRatherThanCrashing)
  {
    // NomadSimulation refuses these at the seam, so the resolver should never see one. It is defended anyway: a
    // scenario or a restored journal is a second way inputs arrive, and an assert in a headless year-long run is a
    // worse outcome than a decision quietly having no target.
    Nomad::World world{4};
    const Nomad::Input inputs[] = {Window(1, Nomad::CompanyId::FromIndex(9), 60)};
    std::vector<Nomad::Event> events;
    Nomad::TickResolver::Advance(world, inputs, events);
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

    const Nomad::Input inputs[] = {Window(2, leftCompany, 60), Window(30, leftCompany, 900),
                                   Window(Neuron::TICKS_PER_DAY + 5, leftCompany, 300)};

    std::vector<Nomad::Event> leftEvents;
    std::vector<Nomad::Event> rightEvents;
    for (Neuron::Tick tick = 1; tick <= 2 * Neuron::TICKS_PER_DAY; ++tick)
    {
      Nomad::TickResolver::Advance(left, inputs, leftEvents);
      Nomad::TickResolver::Advance(right, inputs, rightEvents);
      Assert::AreEqual(left.Hash(), right.Hash(), (L"the two worlds diverged at tick " + std::to_wstring(tick)).c_str());
    }
    Assert::AreEqual(leftEvents.size(), rightEvents.size());
    Assert::AreEqual(std::size_t{3}, leftEvents.size(), L"the three decisions did not all fire across two days");
  }
};

} // namespace GameLogicTests
