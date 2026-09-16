// NeuronCore/MemoryTransport.cpp
#include "pch.h"
#include "MemoryTransport.h"
#include "ByteWriter.h"
#include "Debug.h"

namespace Neuron
{

void MemoryTransport::CreatePair(MemoryTransport& _first, MemoryTransport& _second)
{
  auto toFirst = std::make_shared<Queue>();
  auto toSecond = std::make_shared<Queue>();
  _first.m_inbox = toFirst;
  _first.m_outbox = toSecond;
  _second.m_inbox = toSecond;
  _second.m_outbox = toFirst;
}

bool MemoryTransport::Send(Channel _channel, std::span<const std::byte> _payload)
{
  const std::shared_ptr<Queue> outbox = m_outbox.lock();
  if (outbox == nullptr)
  {
    return false;
  }
  std::vector<std::byte> message;
  ByteWriter writer(message);
  writer.Reserve(Protocol::HEADER_BYTES + _payload.size());
  if (!Protocol::Frame(_channel, _payload, writer))
  {
    return false;
  }
  outbox->push_back(std::move(message));
  return true;
}

bool MemoryTransport::Receive(std::vector<std::byte>& _outMessage)
{
  if (m_inbox == nullptr || m_inbox->empty())
  {
    return false;
  }
  _outMessage = std::move(m_inbox->front());
  m_inbox->pop_front();
  return true;
}

bool MemoryTransport::Connected() const
{
  return m_inbox != nullptr && !m_outbox.expired();
}

void MemoryTransport::Disconnect()
{
  if (m_inbox != nullptr)
  {
    m_inbox->clear();
  }
  m_inbox.reset();
  m_outbox.reset();
}

std::size_t MemoryTransport::PendingCount() const noexcept
{
  return m_inbox == nullptr ? 0 : m_inbox->size();
}

} // namespace Neuron
