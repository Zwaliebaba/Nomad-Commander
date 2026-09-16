// NeuronCore/Serializable.h
#pragma once

#include "ByteReader.h"
#include "ByteWriter.h"

#include <concepts>

namespace Neuron
{

/// What every record that crosses the wire or enters the store looks like (ADR-004): it writes itself, and it is read
/// back by a static function that returns false rather than throwing, leaving the out parameter untouched on failure.
/// The concept is the contract; nothing inherits anything.
template <typename T>
concept Serializable = requires(const T& _value, ByteWriter& _writer, ByteReader& _reader, T& _outValue) {
  { _value.Serialize(_writer) } -> std::same_as<void>;
  { T::Deserialize(_reader, _outValue) } -> std::same_as<bool>;
};

} // namespace Neuron
