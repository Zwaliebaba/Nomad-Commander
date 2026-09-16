// Tests/NeuronCoreTests/ProtocolTests.cpp
#include "pch.h"
#include "ByteReader.h"
#include "ByteWriter.h"
#include "Protocol.h"
#include <cstdint>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{

namespace
{

/// A payload of the given size, filled so that a mistake in the framing shows up as wrong bytes rather than as zeros.
std::vector<std::byte> MakePayload(std::size_t _size)
{
  std::vector<std::byte> payload(_size);
  for (std::size_t index = 0; index < _size; ++index)
  {
    payload[index] = static_cast<std::byte>((index * 31u + 7u) & 0xFFu);
  }
  return payload;
}

bool SamePayload(std::span<const std::byte> _left, std::span<const std::byte> _right)
{
  if (_left.size() != _right.size())
  {
    return false;
  }
  for (std::size_t index = 0; index < _left.size(); ++index)
  {
    if (_left[index] != _right[index])
    {
      return false;
    }
  }
  return true;
}

} // namespace

TEST_CLASS(ProtocolTests)
{
public:
  TEST_METHOD(AZeroLengthPayloadRoundTrips)
  {
    Neuron::ByteWriter writer;
    Assert::IsTrue(Neuron::Protocol::Frame(Neuron::Channel::SessionControl, {}, writer));
    Assert::AreEqual(Neuron::Protocol::HEADER_BYTES, writer.Size());

    Neuron::ByteReader reader(writer.Bytes());
    Neuron::MessageHeader header{};
    std::span<const std::byte> payload;
    Assert::IsTrue(Neuron::Protocol::Unframe(reader, header, payload));
    Assert::AreEqual(Neuron::Protocol::PROTOCOL_VERSION, header.version);
    Assert::IsTrue(header.channel == Neuron::Channel::SessionControl);
    Assert::AreEqual(std::uint32_t{0}, header.payloadBytes);
    Assert::IsTrue(payload.empty());
    Assert::AreEqual(std::size_t{0}, reader.Remaining());
  }

  TEST_METHOD(AMebibytePayloadRoundTrips)
  {
    constexpr std::size_t PAYLOAD_BYTES = std::size_t{1024} * 1024;
    const std::vector<std::byte> payload = MakePayload(PAYLOAD_BYTES);
    Neuron::ByteWriter writer;
    Assert::IsTrue(Neuron::Protocol::Frame(Neuron::Channel::SimulationOutput, payload, writer));
    Assert::AreEqual(Neuron::Protocol::HEADER_BYTES + payload.size(), writer.Size());

    Neuron::ByteReader reader(writer.Bytes());
    Neuron::MessageHeader header{};
    std::span<const std::byte> read;
    Assert::IsTrue(Neuron::Protocol::Unframe(reader, header, read));
    Assert::IsTrue(header.channel == Neuron::Channel::SimulationOutput);
    Assert::IsTrue(SamePayload(payload, read));
  }

  TEST_METHOD(EveryChannelSurvivesTheEnvelope)
  {
    const std::vector<std::byte> payload = MakePayload(5);
    for (const Neuron::Channel channel :
         {Neuron::Channel::SessionControl, Neuron::Channel::SimulationInput, Neuron::Channel::SimulationOutput})
    {
      Neuron::ByteWriter writer;
      Assert::IsTrue(Neuron::Protocol::Frame(channel, payload, writer));
      Neuron::ByteReader reader(writer.Bytes());
      Neuron::MessageHeader header{};
      std::span<const std::byte> read;
      Assert::IsTrue(Neuron::Protocol::Unframe(reader, header, read));
      Assert::IsTrue(header.channel == channel);
      Assert::IsTrue(SamePayload(payload, read));
    }
  }

  TEST_METHOD(MessagesBackToBackUnframeInOrder)
  {
    Neuron::ByteWriter writer;
    Assert::IsTrue(Neuron::Protocol::Frame(Neuron::Channel::SimulationInput, MakePayload(3), writer));
    Assert::IsTrue(Neuron::Protocol::Frame(Neuron::Channel::SessionControl, MakePayload(0), writer));
    Assert::IsTrue(Neuron::Protocol::Frame(Neuron::Channel::SimulationOutput, MakePayload(7), writer));

    Neuron::ByteReader reader(writer.Bytes());
    Neuron::MessageHeader header{};
    std::span<const std::byte> payload;
    Assert::IsTrue(Neuron::Protocol::Unframe(reader, header, payload));
    Assert::IsTrue(header.channel == Neuron::Channel::SimulationInput);
    Assert::AreEqual(std::uint32_t{3}, header.payloadBytes);
    Assert::IsTrue(Neuron::Protocol::Unframe(reader, header, payload));
    Assert::IsTrue(header.channel == Neuron::Channel::SessionControl);
    Assert::IsTrue(Neuron::Protocol::Unframe(reader, header, payload));
    Assert::IsTrue(header.channel == Neuron::Channel::SimulationOutput);
    Assert::AreEqual(std::uint32_t{7}, header.payloadBytes);
    Assert::AreEqual(std::size_t{0}, reader.Remaining());
    Assert::IsFalse(Neuron::Protocol::Unframe(reader, header, payload));
  }

  TEST_METHOD(AnUnknownVersionIsRefused)
  {
    Neuron::ByteWriter writer;
    writer.Write(static_cast<std::uint16_t>(Neuron::Protocol::PROTOCOL_VERSION + 1));
    writer.Write(static_cast<std::uint8_t>(Neuron::Channel::SessionControl));
    writer.Write(std::uint32_t{0});

    Neuron::ByteReader reader(writer.Bytes());
    Neuron::MessageHeader header{};
    std::span<const std::byte> payload;
    Assert::IsFalse(Neuron::Protocol::Unframe(reader, header, payload));
  }

  TEST_METHOD(AnUnknownChannelIsRefusedRatherThanCastIn)
  {
    Neuron::ByteWriter writer;
    writer.Write(Neuron::Protocol::PROTOCOL_VERSION);
    writer.Write(std::uint8_t{9}); // no such channel
    writer.Write(std::uint32_t{0});

    Neuron::ByteReader reader(writer.Bytes());
    Neuron::MessageHeader header{};
    std::span<const std::byte> payload;
    Assert::IsFalse(Neuron::Protocol::Unframe(reader, header, payload));
  }

  TEST_METHOD(ALengthBeyondTheBufferIsRefusedWithoutReadingPastIt)
  {
    Neuron::ByteWriter writer;
    writer.Write(Neuron::Protocol::PROTOCOL_VERSION);
    writer.Write(static_cast<std::uint8_t>(Neuron::Channel::SimulationInput));
    writer.Write(std::uint32_t{1000}); // and nothing behind it
    writer.Write(std::uint8_t{1});

    Neuron::ByteReader reader(writer.Bytes());
    Neuron::MessageHeader header{};
    std::span<const std::byte> payload;
    Assert::IsFalse(Neuron::Protocol::Unframe(reader, header, payload));
    Assert::IsTrue(reader.Failed());
    Assert::IsTrue(payload.empty());
  }

  TEST_METHOD(ALengthBeyondTheLimitIsRefusedBeforeAnythingIsRead)
  {
    Neuron::ByteWriter writer;
    writer.Write(Neuron::Protocol::PROTOCOL_VERSION);
    writer.Write(static_cast<std::uint8_t>(Neuron::Channel::SimulationInput));
    writer.Write(std::uint32_t{0xFFFFFFFFu});

    Neuron::ByteReader reader(writer.Bytes());
    Neuron::MessageHeader header{};
    std::span<const std::byte> payload;
    Assert::IsFalse(Neuron::Protocol::Unframe(reader, header, payload));
    Assert::IsFalse(reader.Failed()); // refused by the limit, not by running out of bytes
  }

  TEST_METHOD(ATruncatedEnvelopeIsRefused)
  {
    Neuron::ByteWriter writer;
    Assert::IsTrue(Neuron::Protocol::Frame(Neuron::Channel::SimulationInput, MakePayload(4), writer));
    const std::span<const std::byte> whole = writer.Bytes();
    for (std::size_t length = 0; length < whole.size(); ++length)
    {
      Neuron::ByteReader reader(whole.subspan(0, length));
      Neuron::MessageHeader header{};
      std::span<const std::byte> payload;
      Assert::IsFalse(Neuron::Protocol::Unframe(reader, header, payload));
    }
  }
};

} // namespace NeuronCoreTests
