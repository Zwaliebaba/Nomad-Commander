// Tests/GameLogicTests/WireTests.cpp
#include "pch.h"
#include "ByteReader.h"
#include "ByteWriter.h"
#include "Event.h"
#include "Explanation.h"
#include "Input.h"
#include "WireEvent.h"
#include "WireInput.h"

#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace GameLogicTests
{

namespace
{

/// GDD §9's worked example, as the record the simulation would emit for it.
[[nodiscard]] Nomad::Explanation TheVarnRevocation()
{
  Nomad::Explanation explanation{};
  explanation.believer = Nomad::EmpireId::FromIndex(0);
  explanation.confidence = Neuron::Hundredths::FromRaw(71);
  explanation.actor = Nomad::CharacterId::FromIndex(4);
  explanation.reason = Nomad::ReasonCode::ConvoyAttacked;
  explanation.evidenceFor = {
    {"detected within two jumps", Neuron::Hundredths::FromRaw(25)},
    {"survivors identified a raider hull", Neuron::Hundredths::FromRaw(15)},
    {"Varn-marked fuel sold at Oren Prime by your outpost", Neuron::Hundredths::FromRaw(30)},
  };
  explanation.evidenceAgainst = {
    {"your recorded route conflicts with the timing", Neuron::Hundredths::FromRaw(-30)},
  };
  return explanation;
}

template <typename T> [[nodiscard]] std::vector<std::byte> Written(const T& _value)
{
  Neuron::ByteWriter writer;
  Serialize(writer, _value);
  const std::span<const std::byte> bytes = writer.Bytes();
  return std::vector<std::byte>{bytes.begin(), bytes.end()};
}

} // namespace

TEST_CLASS(WireTests)
{
public:
  TEST_METHOD(AnExplanationSurvivesTheWireWithEveryItemOfEvidence)
  {
    // R19's payload is the thing most likely to be quietly dropped: an event whose explanation arrived empty would
    // still render, and the player would simply never learn why anything happened.
    const Nomad::WireExplanation sent = Nomad::ToWire(TheVarnRevocation());
    const std::vector<std::byte> bytes = Written(sent);

    Nomad::WireExplanation received{};
    Neuron::ByteReader reader{bytes};
    Assert::IsTrue(Nomad::Deserialize(reader, received), L"the explanation could not be read back");
    Assert::AreEqual(std::size_t{0}, reader.Remaining());

    Assert::AreEqual(sent.believerEmpireIndex, received.believerEmpireIndex);
    Assert::AreEqual(sent.confidence.Raw(), received.confidence.Raw());
    Assert::AreEqual(sent.actorCharacterIndex, received.actorCharacterIndex);
    Assert::IsTrue(sent.reason == received.reason);
    Assert::AreEqual(std::size_t{3}, received.evidenceFor.size());
    Assert::AreEqual(std::size_t{1}, received.evidenceAgainst.size());
    Assert::AreEqual(std::string{"detected within two jumps"}, received.evidenceFor[0].text);
    Assert::AreEqual(-30, received.evidenceAgainst[0].weight.Raw());
  }

  TEST_METHOD(AnEventCarriesItsExplanationAcrossTheWire)
  {
    Nomad::EventSubjects subjects{};
    subjects.company = Nomad::CompanyId::FromIndex(0);
    subjects.empire = Nomad::EmpireId::FromIndex(0);
    subjects.fleet = Nomad::FleetId::FromIndex(2);
    const Nomad::Event event{99, Nomad::EventKind::FleetArrived, subjects, TheVarnRevocation()};

    const std::vector<std::byte> bytes = Written(Nomad::ToWire(event));
    Nomad::WireEvent received{};
    Neuron::ByteReader reader{bytes};
    Assert::IsTrue(Nomad::Deserialize(reader, received));
    Assert::AreEqual(std::size_t{0}, reader.Remaining());

    Assert::AreEqual(Neuron::Tick{99}, received.tick);
    Assert::IsTrue(received.kind == Nomad::EventKind::FleetArrived);
    Assert::AreEqual(2u, received.fleetIndex);
    // An id that was never set arrives as "none" rather than as index zero, which would name the first row.
    Assert::AreEqual(Nomad::WIRE_INDEX_NONE, received.systemIndex);
    Assert::AreEqual(std::size_t{3}, received.explanation.evidenceFor.size());
  }

  TEST_METHOD(AnInputSurvivesTheWire)
  {
    Nomad::Input input{};
    input.applyAtTick = 4321;
    input.kind = Nomad::InputKind::SetActiveWindow;
    input.company = Nomad::CompanyId::FromIndex(1);
    input.activeWindowStartTickOfDay = 18 * Neuron::TICKS_PER_HOUR;
    input.activeWindowLengthTicks = 2 * Neuron::TICKS_PER_HOUR;

    const std::vector<std::byte> bytes = Written(Nomad::ToWire(input));
    Nomad::WireInput received{};
    Neuron::ByteReader reader{bytes};
    Assert::IsTrue(Nomad::Deserialize(reader, received));
    Assert::AreEqual(std::size_t{0}, reader.Remaining());
    Assert::AreEqual(Neuron::Tick{4321}, received.applyAtTick);
    Assert::AreEqual(1u, received.companyIndex);
    Assert::AreEqual(18 * Neuron::TICKS_PER_HOUR, received.activeWindowStartTickOfDay);
  }

  TEST_METHOD(EveryTruncationOfEveryRecordIsRefused)
  {
    // The same sweep NC-040 runs over a store, applied to each wire record: a length that was not checked shows up
    // here and nowhere else, and the wire is the one place the bytes come from somewhere this build did not write.
    const std::vector<std::byte> explanation = Written(Nomad::ToWire(TheVarnRevocation()));
    for (std::size_t length = 0; length < explanation.size(); ++length)
    {
      Nomad::WireExplanation value{};
      Neuron::ByteReader reader{std::span<const std::byte>{explanation.data(), length}};
      Assert::IsFalse(Nomad::Deserialize(reader, value),
                      (L"an explanation truncated to " + std::to_wstring(length) + L" bytes was accepted").c_str());
    }

    Nomad::Input input{};
    input.applyAtTick = 10;
    input.kind = Nomad::InputKind::SetActiveWindow;
    input.company = Nomad::CompanyId::FromIndex(0);
    const std::vector<std::byte> wireInput = Written(Nomad::ToWire(input));
    for (std::size_t length = 0; length < wireInput.size(); ++length)
    {
      Nomad::WireInput value{};
      Neuron::ByteReader reader{std::span<const std::byte>{wireInput.data(), length}};
      Assert::IsFalse(Nomad::Deserialize(reader, value),
                      (L"an input truncated to " + std::to_wstring(length) + L" bytes was accepted").c_str());
    }
  }

  TEST_METHOD(AnEnumeratorTheSchemaDoesNotHoldIsRefused)
  {
    // A kind byte this build has no case for must be refused, not cast: the client and the host can be different
    // builds, and a WireEvent of kind 200 is a switch nobody wrote.
    Nomad::Input input{};
    input.applyAtTick = 10;
    input.kind = Nomad::InputKind::SetActiveWindow;
    input.company = Nomad::CompanyId::FromIndex(0);
    std::vector<std::byte> bytes = Written(Nomad::ToWire(input));

    // The kind follows the eight-byte tick.
    bytes[sizeof(Neuron::Tick)] = static_cast<std::byte>(200);
    Nomad::WireInput value{};
    Neuron::ByteReader reader{bytes};
    Assert::IsFalse(Nomad::Deserialize(reader, value), L"an input kind of 200 was accepted");
  }

  TEST_METHOD(TheSentenceIsComposedFromWhatTheClientWasTold)
  {
    // GDD §9's form, and the property that matters more than the wording: Compose takes the *wire* record, so the
    // sentence can hold nothing the client was not also sent (ADR-018).
    const Nomad::WireExplanation wire = Nomad::ToWire(TheVarnRevocation());
    const std::string sentence = Nomad::ExplanationText::Compose(wire);

    Assert::IsTrue(sentence.starts_with("Why? They believe your fleet attacked their convoy, confidence 71 percent."),
                   (L"the sentence does not open the way GDD 9 does; it reads: " + std::wstring(sentence.begin(), sentence.end())).c_str());
    Assert::IsTrue(sentence.find("For: detected within two jumps; survivors identified a raider hull") != std::string::npos,
                   L"the evidence for is missing or joined wrongly");
    Assert::IsTrue(sentence.find("Against: your recorded route conflicts with the timing.") != std::string::npos,
                   L"the evidence against is missing");
  }

  TEST_METHOD(AnExplanationWithNoBeliefBehindItIsOneSentence)
  {
    // Most events are not accusations. They still carry an explanation (R19) and it still has to read as English.
    const std::string sentence = Nomad::ExplanationText::Compose(Nomad::ToWire(Nomad::Because(Nomad::ReasonCode::FleetArrived)));
    Assert::AreEqual(std::string{"a fleet arrived."}, sentence);
    Assert::IsTrue(sentence.find("They believe") == std::string::npos, L"an event nobody believed anything about claims a belief");
    Assert::IsTrue(sentence.find("confidence") == std::string::npos, L"a confidence of nothing was printed");
  }

  TEST_METHOD(EveryReasonCodeHasWordsForIt)
  {
    // A reason added without a claim would compose as the fallback, which reads like a bug in the game rather than a
    // gap in a table. This catches it at the moment the enumerator grows.
    for (std::uint16_t reason = 0; reason < Nomad::REASON_CODE_COUNT; ++reason)
    {
      const std::string claim = Nomad::ExplanationText::ClaimOf(static_cast<Nomad::ReasonCode>(reason));
      Assert::IsFalse(claim.empty(), L"a reason code composes to nothing");
      if (reason != static_cast<std::uint16_t>(Nomad::ReasonCode::Unknown))
      {
        Assert::AreNotEqual(Nomad::ExplanationText::ClaimOf(Nomad::ReasonCode::Unknown), claim,
                            (L"reason code " + std::to_wstring(reason) + L" has no words of its own").c_str());
      }
    }
  }
};

} // namespace GameLogicTests
