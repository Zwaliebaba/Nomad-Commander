// NeuronCore/Protocol.cpp
#include "pch.h"
#include "Protocol.h"

namespace Neuron
{

bool Protocol::Frame(Channel _channel, std::span<const std::byte> _payload, ByteWriter& _writer)
{
  if (_payload.size() > MAX_PAYLOAD_BYTES)
  {
    return false;
  }
  _writer.Write(PROTOCOL_VERSION);
  _writer.Write(static_cast<std::uint8_t>(_channel));
  _writer.Write(static_cast<std::uint32_t>(_payload.size()));
  _writer.WriteBytes(_payload);
  return true;
}

bool Protocol::Unframe(ByteReader& _reader, MessageHeader& _outHeader, std::span<const std::byte>& _outPayload)
{
  std::uint16_t version = 0;
  std::uint8_t channelValue = 0;
  std::uint32_t payloadBytes = 0;
  if (!_reader.Read(version) || !_reader.Read(channelValue) || !_reader.Read(payloadBytes))
  {
    return false;
  }
  if (version != PROTOCOL_VERSION)
  {
    return false;
  }
  // The value is checked against the known channels before it becomes one: a corrupt stream must not produce an
  // enumerator that does not exist.
  if (!IsKnownChannel(channelValue))
  {
    return false;
  }
  if (payloadBytes > MAX_PAYLOAD_BYTES)
  {
    return false;
  }
  std::span<const std::byte> payload;
  if (!_reader.ReadBytes(payloadBytes, payload))
  {
    return false;
  }
  _outHeader.version = version;
  _outHeader.channel = static_cast<Channel>(channelValue);
  _outHeader.payloadBytes = payloadBytes;
  _outPayload = payload;
  return true;
}

} // namespace Neuron
