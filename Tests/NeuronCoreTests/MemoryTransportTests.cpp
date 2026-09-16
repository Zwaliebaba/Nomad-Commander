// Tests/NeuronCoreTests/MemoryTransportTests.cpp
#include "pch.h"
#include "ByteReader.h"
#include "MemoryTransport.h"
#include "Protocol.h"
#include <cstdint>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{

namespace
{

/// Sends one std::uint32_t on a channel, the way a session control message or a small input looks.
bool SendValue(Neuron::Transport& _transport, Neuron::Channel _channel, std::uint32_t _value)
{
  std::vector<std::byte> payload;
  Neuron::ByteWriter writer(payload);
  writer.Write(_value);
  return _transport.Send(_channel, payload);
}

/// Receives one message and splits it, the way every caller of a transport does.
bool ReceiveValue(Neuron::Transport& _transport, Neuron::Channel& _outChannel, std::uint32_t& _outValue)
{
  std::vector<std::byte> message;
  if (!_transport.Receive(message))
  {
    return false;
  }
  Neuron::ByteReader reader(message);
  Neuron::MessageHeader header{};
  std::span<const std::byte> payload;
  if (!Neuron::Protocol::Unframe(reader, header, payload))
  {
    return false;
  }
  Neuron::ByteReader payloadReader(payload);
  if (!payloadReader.Read(_outValue))
  {
    return false;
  }
  _outChannel = header.channel;
  return true;
}

} // namespace

TEST_CLASS(MemoryTransportTests)
{
public:
  TEST_METHOD(WhatOneEndSendsTheOtherReceives)
  {
    Neuron::MemoryTransport client;
    Neuron::MemoryTransport host;
    Neuron::MemoryTransport::CreatePair(client, host);
    Assert::IsTrue(client.Connected());
    Assert::IsTrue(host.Connected());

    Assert::IsTrue(SendValue(client, Neuron::Channel::SimulationInput, 42));
    Neuron::Channel channel = Neuron::Channel::SessionControl;
    std::uint32_t value = 0;
    Assert::IsTrue(ReceiveValue(host, channel, value));
    Assert::IsTrue(channel == Neuron::Channel::SimulationInput);
    Assert::AreEqual(std::uint32_t{42}, value);

    // And the other way, on its own channel.
    Assert::IsTrue(SendValue(host, Neuron::Channel::SimulationOutput, 7));
    Assert::IsTrue(ReceiveValue(client, channel, value));
    Assert::IsTrue(channel == Neuron::Channel::SimulationOutput);
    Assert::AreEqual(std::uint32_t{7}, value);
  }

  TEST_METHOD(AnEndpointDoesNotReceiveItsOwnMessages)
  {
    Neuron::MemoryTransport client;
    Neuron::MemoryTransport host;
    Neuron::MemoryTransport::CreatePair(client, host);
    Assert::IsTrue(SendValue(client, Neuron::Channel::SimulationInput, 1));

    std::vector<std::byte> message;
    Assert::IsFalse(client.Receive(message));
    Assert::AreEqual(std::size_t{0}, client.PendingCount());
    Assert::AreEqual(std::size_t{1}, host.PendingCount());
  }

  TEST_METHOD(ReceiveOnAnEmptyQueueReturnsFalseAndLeavesTheBufferAlone)
  {
    Neuron::MemoryTransport client;
    Neuron::MemoryTransport host;
    Neuron::MemoryTransport::CreatePair(client, host);

    std::vector<std::byte> message;
    message.push_back(std::byte{0xAB});
    Assert::IsFalse(host.Receive(message));
    Assert::AreEqual(std::size_t{1}, message.size());
    Assert::AreEqual(std::uint8_t{0xAB}, std::to_integer<std::uint8_t>(message[0]));
  }

  TEST_METHOD(TenThousandMessagesArriveWholeAndInOrder)
  {
    Neuron::MemoryTransport client;
    Neuron::MemoryTransport host;
    Neuron::MemoryTransport::CreatePair(client, host);

    constexpr std::uint32_t COUNT = 10000;
    for (std::uint32_t value = 0; value < COUNT; ++value)
    {
      Assert::IsTrue(SendValue(client, Neuron::Channel::SimulationInput, value));
    }
    Assert::AreEqual(static_cast<std::size_t>(COUNT), host.PendingCount());

    for (std::uint32_t expected = 0; expected < COUNT; ++expected)
    {
      Neuron::Channel channel = Neuron::Channel::SessionControl;
      std::uint32_t value = 0;
      Assert::IsTrue(ReceiveValue(host, channel, value));
      Assert::AreEqual(expected, value);
      Assert::IsTrue(channel == Neuron::Channel::SimulationInput);
    }
    std::vector<std::byte> message;
    Assert::IsFalse(host.Receive(message));
  }

  TEST_METHOD(AMessageArrivesWholeHoweverLargeItIs)
  {
    Neuron::MemoryTransport client;
    Neuron::MemoryTransport host;
    Neuron::MemoryTransport::CreatePair(client, host);

    constexpr std::size_t PAYLOAD_BYTES = std::size_t{256} * 1024;
    std::vector<std::byte> payload(PAYLOAD_BYTES);
    for (std::size_t index = 0; index < payload.size(); ++index)
    {
      payload[index] = static_cast<std::byte>(index & 0xFFu);
    }
    Assert::IsTrue(client.Send(Neuron::Channel::SimulationOutput, payload));

    std::vector<std::byte> message;
    Assert::IsTrue(host.Receive(message));
    Neuron::ByteReader reader(message);
    Neuron::MessageHeader header{};
    std::span<const std::byte> received;
    Assert::IsTrue(Neuron::Protocol::Unframe(reader, header, received));
    Assert::AreEqual(payload.size(), received.size());
    Assert::IsTrue(payload[payload.size() - 1] == received[received.size() - 1]);
  }

  TEST_METHOD(AnUnpairedTransportSendsNothingAndIsNotConnected)
  {
    Neuron::MemoryTransport alone;
    Assert::IsFalse(alone.Connected());
    Assert::IsFalse(SendValue(alone, Neuron::Channel::SessionControl, 1));
    std::vector<std::byte> message;
    Assert::IsFalse(alone.Receive(message));
  }

  TEST_METHOD(DisconnectingOneEndDisconnectsBothAndDropsWhatWasQueued)
  {
    Neuron::MemoryTransport client;
    Neuron::MemoryTransport host;
    Neuron::MemoryTransport::CreatePair(client, host);
    Assert::IsTrue(SendValue(client, Neuron::Channel::SimulationInput, 1));
    Assert::AreEqual(std::size_t{1}, host.PendingCount());

    host.Disconnect();
    Assert::IsFalse(host.Connected());
    Assert::IsFalse(client.Connected());
    Assert::AreEqual(std::size_t{0}, host.PendingCount());
    Assert::IsFalse(SendValue(client, Neuron::Channel::SimulationInput, 2));
  }
};

} // namespace NeuronCoreTests
