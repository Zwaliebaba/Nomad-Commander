// Tests/NeuronCoreTests/ByteStreamTests.cpp
#include "pch.h"
#include "ByteReader.h"
#include "ByteWriter.h"
#include "Hundredths.h"
#include "Id.h"
#include "Random.h"
#include "Serializable.h"
#include "Tick.h"
#include <array>
#include <limits>
#include <string>
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace NeuronCoreTests
{

namespace
{

struct ConvoyTag
{
};
using ConvoyId = Neuron::Id<ConvoyTag>;

/// A record in the shape ADR-004 fixes, used to check that the concept matches what the tree actually writes.
struct Sighting
{
  Neuron::Tick observedAtTick;
  ConvoyId convoy;
  Neuron::Hundredths reliability;
  bool marked;
  std::string observer;

  void Serialize(Neuron::ByteWriter& _writer) const
  {
    _writer.WriteTick(observedAtTick);
    _writer.WriteId(convoy);
    _writer.WriteHundredths(reliability);
    _writer.WriteBool(marked);
    _writer.WriteString(observer);
  }

  [[nodiscard]] static bool Deserialize(Neuron::ByteReader& _reader, Sighting& _outSighting)
  {
    Sighting value{};
    if (!_reader.ReadTick(value.observedAtTick) || !_reader.ReadId(value.convoy) || !_reader.ReadHundredths(value.reliability) ||
        !_reader.ReadBool(value.marked) || !_reader.ReadString(value.observer))
    {
      return false;
    }
    _outSighting = value;
    return true;
  }
};

static_assert(Neuron::Serializable<Sighting>);

} // namespace

TEST_CLASS(ByteStreamTests)
{
public:
  TEST_METHOD(EveryIntegerTypeRoundTripsAtItsExtremes)
  {
    Neuron::ByteWriter writer;
    writer.Write(std::uint8_t{0});
    writer.Write(std::numeric_limits<std::uint8_t>::max());
    writer.Write(std::numeric_limits<std::uint16_t>::max());
    writer.Write(std::numeric_limits<std::uint32_t>::max());
    writer.Write(std::numeric_limits<std::uint64_t>::max());
    writer.Write(std::numeric_limits<std::int8_t>::min());
    writer.Write(std::numeric_limits<std::int8_t>::max());
    writer.Write(std::numeric_limits<std::int16_t>::min());
    writer.Write(std::numeric_limits<std::int32_t>::min());
    writer.Write(std::numeric_limits<std::int64_t>::min());
    writer.Write(std::numeric_limits<std::int64_t>::max());
    writer.WriteBool(true);
    writer.WriteBool(false);

    Neuron::ByteReader reader(writer.Bytes());
    std::uint8_t unsigned8 = 1;
    std::uint8_t unsigned8Max = 0;
    std::uint16_t unsigned16Max = 0;
    std::uint32_t unsigned32Max = 0;
    std::uint64_t unsigned64Max = 0;
    std::int8_t signed8Min = 0;
    std::int8_t signed8Max = 0;
    std::int16_t signed16Min = 0;
    std::int32_t signed32Min = 0;
    std::int64_t signed64Min = 0;
    std::int64_t signed64Max = 0;
    bool yes = false;
    bool no = true;
    Assert::IsTrue(reader.Read(unsigned8));
    Assert::IsTrue(reader.Read(unsigned8Max));
    Assert::IsTrue(reader.Read(unsigned16Max));
    Assert::IsTrue(reader.Read(unsigned32Max));
    Assert::IsTrue(reader.Read(unsigned64Max));
    Assert::IsTrue(reader.Read(signed8Min));
    Assert::IsTrue(reader.Read(signed8Max));
    Assert::IsTrue(reader.Read(signed16Min));
    Assert::IsTrue(reader.Read(signed32Min));
    Assert::IsTrue(reader.Read(signed64Min));
    Assert::IsTrue(reader.Read(signed64Max));
    Assert::IsTrue(reader.ReadBool(yes));
    Assert::IsTrue(reader.ReadBool(no));

    Assert::AreEqual(std::uint8_t{0}, unsigned8);
    Assert::AreEqual(std::numeric_limits<std::uint8_t>::max(), unsigned8Max);
    Assert::AreEqual(std::numeric_limits<std::uint16_t>::max(), unsigned16Max);
    Assert::AreEqual(std::numeric_limits<std::uint32_t>::max(), unsigned32Max);
    Assert::AreEqual(std::numeric_limits<std::uint64_t>::max(), unsigned64Max);
    Assert::AreEqual(std::numeric_limits<std::int8_t>::min(), signed8Min);
    Assert::AreEqual(std::numeric_limits<std::int8_t>::max(), signed8Max);
    Assert::AreEqual(std::numeric_limits<std::int16_t>::min(), signed16Min);
    Assert::AreEqual(std::numeric_limits<std::int32_t>::min(), signed32Min);
    Assert::AreEqual(std::numeric_limits<std::int64_t>::min(), signed64Min);
    Assert::AreEqual(std::numeric_limits<std::int64_t>::max(), signed64Max);
    Assert::IsTrue(yes);
    Assert::IsFalse(no);
    Assert::AreEqual(std::size_t{0}, reader.Remaining());
    Assert::IsFalse(reader.Failed());
  }

  TEST_METHOD(TheLayoutIsLittleEndianWhateverTheHostIs)
  {
    Neuron::ByteWriter writer;
    writer.Write(std::uint32_t{0x01020304u});
    const std::span<const std::byte> bytes = writer.Bytes();
    Assert::AreEqual(std::size_t{4}, bytes.size());
    Assert::AreEqual(std::uint8_t{0x04}, std::to_integer<std::uint8_t>(bytes[0]));
    Assert::AreEqual(std::uint8_t{0x03}, std::to_integer<std::uint8_t>(bytes[1]));
    Assert::AreEqual(std::uint8_t{0x02}, std::to_integer<std::uint8_t>(bytes[2]));
    Assert::AreEqual(std::uint8_t{0x01}, std::to_integer<std::uint8_t>(bytes[3]));
  }

  TEST_METHOD(AStringWithMultiByteCharactersSurvives)
  {
    // Three characters of two, three and four UTF-8 bytes; the writer does not inspect them and the reader hands back
    // the same bytes.
    const std::string text = "Kessel \xC3\xA9\xE2\x82\xAC\xF0\x9F\x9A\x80";
    Neuron::ByteWriter writer;
    writer.WriteString(text);
    writer.WriteString(std::string{});
    Assert::AreEqual(std::size_t{4} + text.size() + std::size_t{4}, writer.Size());

    Neuron::ByteReader reader(writer.Bytes());
    std::string read;
    std::string empty = "not empty";
    Assert::IsTrue(reader.ReadString(read));
    Assert::IsTrue(reader.ReadString(empty));
    Assert::AreEqual(text, read);
    Assert::IsTrue(empty.empty());
  }

  TEST_METHOD(ReadingPastTheEndFailsWithoutTouchingTheBuffer)
  {
    const std::array<std::byte, 1> oneByte = {std::byte{0x7F}};
    // Every reader wider than the buffer fails, leaves its out parameter alone and sets the sticky flag.
    {
      Neuron::ByteReader reader(oneByte);
      std::uint16_t value = 0xAAAA;
      Assert::IsFalse(reader.Read(value));
      Assert::AreEqual(std::uint16_t{0xAAAA}, value);
      Assert::IsTrue(reader.Failed());
    }
    {
      Neuron::ByteReader reader(oneByte);
      std::uint64_t value = 7;
      Assert::IsFalse(reader.Read(value));
      Assert::IsTrue(reader.Failed());
    }
    {
      Neuron::ByteReader reader(oneByte);
      Neuron::Tick tick = 5;
      Assert::IsFalse(reader.ReadTick(tick));
      Assert::AreEqual(Neuron::Tick{5}, tick);
    }
    {
      Neuron::ByteReader reader(oneByte);
      Neuron::Hundredths value = Neuron::HUNDREDTHS_UNITY;
      Assert::IsFalse(reader.ReadHundredths(value));
      Assert::IsTrue(value == Neuron::HUNDREDTHS_UNITY);
    }
    {
      Neuron::ByteReader reader(oneByte);
      ConvoyId id = ConvoyId::FromIndex(3);
      Assert::IsFalse(reader.ReadId(id));
      Assert::AreEqual(std::uint32_t{3}, id.Index());
    }
    {
      Neuron::ByteReader reader(oneByte);
      std::string text = "kept";
      Assert::IsFalse(reader.ReadString(text));
      Assert::AreEqual(std::string{"kept"}, text);
    }
    {
      Neuron::ByteReader reader(oneByte);
      std::span<const std::byte> bytes;
      Assert::IsFalse(reader.ReadBytes(2, bytes));
      Assert::IsTrue(bytes.empty());
    }
    {
      Neuron::ByteReader reader(oneByte);
      Assert::IsFalse(reader.Skip(2));
      Assert::IsTrue(reader.Failed());
    }
    {
      // A one-byte read succeeds, and the reader is then empty rather than failed.
      Neuron::ByteReader reader(oneByte);
      std::uint8_t value = 0;
      Assert::IsTrue(reader.Read(value));
      Assert::AreEqual(std::uint8_t{0x7F}, value);
      Assert::IsFalse(reader.Failed());
      Assert::AreEqual(std::size_t{0}, reader.Remaining());
    }
  }

  TEST_METHOD(FailureIsStickyAcrossLaterReads)
  {
    Neuron::ByteWriter writer;
    writer.Write(std::uint32_t{42});
    Neuron::ByteReader reader(writer.Bytes());
    std::uint64_t tooWide = 0;
    Assert::IsFalse(reader.Read(tooWide));
    Assert::IsTrue(reader.Failed());
    // The four bytes are still there, and a later read of a size that would fit still refuses.
    std::uint32_t narrow = 0;
    Assert::IsFalse(reader.Read(narrow));
    Assert::AreEqual(std::uint32_t{0}, narrow);
  }

  TEST_METHOD(ACorruptLengthCannotAskForMoreThanTheBufferHolds)
  {
    Neuron::ByteWriter writer;
    writer.Write(std::uint32_t{0xFFFFFFFFu}); // a length of four billion, and nothing behind it
    Neuron::ByteReader reader(writer.Bytes());
    std::string text = "kept";
    Assert::IsFalse(reader.ReadString(text));
    Assert::IsTrue(reader.Failed());
    Assert::AreEqual(std::string{"kept"}, text);
  }

  TEST_METHOD(AWriterCanAppendToABufferItWasLent)
  {
    std::vector<std::byte> buffer;
    buffer.push_back(std::byte{0xEE});
    {
      Neuron::ByteWriter writer(buffer);
      writer.Write(std::uint16_t{0x0102u});
      Assert::AreEqual(std::size_t{3}, writer.Size());
    }
    Assert::AreEqual(std::size_t{3}, buffer.size());
    Assert::AreEqual(std::uint8_t{0xEE}, std::to_integer<std::uint8_t>(buffer[0]));
    Assert::AreEqual(std::uint8_t{0x02}, std::to_integer<std::uint8_t>(buffer[1]));
    Assert::AreEqual(std::uint8_t{0x01}, std::to_integer<std::uint8_t>(buffer[2]));
  }

  TEST_METHOD(ASerializableRecordRoundTrips)
  {
    const Sighting written{Neuron::TicksFromHours(9), ConvoyId::FromIndex(12), Neuron::Hundredths::FromRaw(58), false, "Kessel picket"};
    Neuron::ByteWriter writer;
    written.Serialize(writer);

    Neuron::ByteReader reader(writer.Bytes());
    Sighting read{};
    Assert::IsTrue(Sighting::Deserialize(reader, read));
    Assert::AreEqual(written.observedAtTick, read.observedAtTick);
    Assert::AreEqual(written.convoy.Index(), read.convoy.Index());
    Assert::IsTrue(written.reliability == read.reliability);
    Assert::AreEqual(written.marked, read.marked);
    Assert::AreEqual(written.observer, read.observer);
    Assert::AreEqual(std::size_t{0}, reader.Remaining());
  }

  TEST_METHOD(ATruncatedRecordFailsAndLeavesTheOutParameterAlone)
  {
    const Sighting written{Neuron::TicksFromHours(9), ConvoyId::FromIndex(12), Neuron::Hundredths::FromRaw(58), false, "Kessel picket"};
    Neuron::ByteWriter writer;
    written.Serialize(writer);
    const std::span<const std::byte> whole = writer.Bytes();

    for (std::size_t length = 0; length < whole.size(); ++length)
    {
      Neuron::ByteReader reader(whole.subspan(0, length));
      Sighting read{Neuron::Tick{1}, ConvoyId::FromIndex(99), Neuron::HUNDREDTHS_UNITY, true, "untouched"};
      Assert::IsFalse(Sighting::Deserialize(reader, read));
      Assert::AreEqual(std::string{"untouched"}, read.observer);
    }
  }

  TEST_METHOD(ARandomStateRoundTripsAndAnEvenIncrementIsRejected)
  {
    Neuron::Random source(42, 54);
    for (int i = 0; i < 3; ++i)
    {
      (void)source.Next();
    }
    Neuron::ByteWriter writer;
    source.WriteState(writer);

    Neuron::ByteReader reader(writer.Bytes());
    Neuron::Random restored(1);
    Assert::IsTrue(restored.ReadState(reader));
    for (int i = 0; i < 10; ++i)
    {
      Assert::AreEqual(source.Next(), restored.Next());
    }

    // An even increment is a corrupt store: the generator's period depends on it being odd (ADR-002).
    Neuron::ByteWriter corrupt;
    corrupt.Write(std::uint64_t{1});
    corrupt.Write(std::uint64_t{2});
    Neuron::ByteReader corruptReader(corrupt.Bytes());
    Neuron::Random target(1);
    Assert::IsFalse(target.ReadState(corruptReader));
  }

  TEST_METHOD(AHundredThousandIntegersRoundTrip)
  {
    constexpr std::uint32_t COUNT = 100000;
    Neuron::ByteWriter writer;
    writer.Reserve(COUNT * sizeof(std::uint32_t));
    for (std::uint32_t value = 0; value < COUNT; ++value)
    {
      writer.Write(value);
    }
    Assert::AreEqual(static_cast<std::size_t>(COUNT) * sizeof(std::uint32_t), writer.Size());

    Neuron::ByteReader reader(writer.Bytes());
    for (std::uint32_t expected = 0; expected < COUNT; ++expected)
    {
      std::uint32_t value = 0;
      Assert::IsTrue(reader.Read(value));
      Assert::AreEqual(expected, value);
    }
    Assert::AreEqual(std::size_t{0}, reader.Remaining());
  }
};

} // namespace NeuronCoreTests
