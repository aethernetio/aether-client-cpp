/*
 * Copyright 2024 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <unity.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "aether-miscpp/serialization/binary_archive.h"

#include "aether/packed_size.h"
#include "aether/transport/data_packet_collector.h"
#include "aether/vector_buffer.h"

namespace ae::test_data_pc {
void test_DataPacketCollectorEmpty() {
  StreamDataPacketCollector collector;
  auto data_packet = collector.PopPacket();
  TEST_ASSERT(data_packet.empty());
}

std::vector<std::uint8_t> MakeStreamPacket(std::vector<std::uint8_t> data) {
  std::vector<std::uint8_t> packet_size_data;
  auto buffer = VectorBuffer<PacketSize>{packet_size_data};
  TEST_ASSERT(buffer.Write(seri::SizeWriteTag{data.size()}));

  data.insert(data.begin(), packet_size_data.begin(), packet_size_data.end());

  return std::move(data);
}

std::vector<std::uint8_t> TestPacket() {
  std::vector<std::uint8_t> packet;
  auto archive = seri::BinaryArchive{seri::BinaryVectorBuffer<>{packet}};
  TEST_ASSERT(archive.Save(std::string{"Hello"}));
  TEST_ASSERT(archive.Save(int{12}));
  TEST_ASSERT(archive.Save(float{12.42}));

  return MakeStreamPacket(std::move(packet));
}

inline void AssertPacket(std::vector<std::uint8_t> const& data_packet) {
  auto archive = seri::BinaryArchive{
      seri::BinaryVectorBuffer<>{
          const_cast<std::vector<std::uint8_t>&>(data_packet)},  // NOLINT
  };
  std::string str;
  int i{};
  float f{};
  TEST_ASSERT(archive.Load(str));
  TEST_ASSERT(archive.Load(i));
  TEST_ASSERT(archive.Load(f));
  TEST_ASSERT_EQUAL_STRING("Hello", str.c_str());
  TEST_ASSERT_EQUAL(12, i);
  TEST_ASSERT_EQUAL(12.42, f);
}

void test_AddOnePacket() {
  StreamDataPacketCollector collector;
  auto packet = TestPacket();

  collector.AddData(packet.data(), packet.size());
  auto data_packet = collector.PopPacket();

  TEST_ASSERT(!data_packet.empty());
  AssertPacket(data_packet);

  auto p = collector.PopPacket();
  TEST_ASSERT(p.empty());
}

void test_AddFewPackets() {
  StreamDataPacketCollector collector;
  for (auto i = 0; i < 2; ++i) {
    auto packet = TestPacket();
    collector.AddData(packet.data(), packet.size());
  }
  for (auto i = 0; i < 2; ++i) {
    auto data_packet = collector.PopPacket();

    TEST_ASSERT(!data_packet.empty());
    AssertPacket(data_packet);
  }

  auto p = collector.PopPacket();
  TEST_ASSERT(p.empty());
}

void test_AddBigPacket() {
  StreamDataPacketCollector collector;
  auto garbage = MakeStreamPacket(std::vector<std::uint8_t>(1200));
  collector.AddData(garbage.data(), garbage.size());

  auto data_packet = collector.PopPacket();
  TEST_ASSERT_EQUAL(1200, data_packet.size());
}

void test_AddFewPacketInOne() {
  StreamDataPacketCollector collector;
  std::vector<std::uint8_t> cumulative_packet;

  for (auto i = 0; i < 2; ++i) {
    auto packet = TestPacket();
    cumulative_packet.insert(cumulative_packet.begin(), std::begin(packet),
                             std::end(packet));
  }

  collector.AddData(cumulative_packet.data(), cumulative_packet.size());

  for (auto i = 0; i < 2; ++i) {
    auto data_packet = collector.PopPacket();

    TEST_ASSERT(!data_packet.empty());
    AssertPacket(data_packet);
  }
  auto p = collector.PopPacket();
  TEST_ASSERT(p.empty());
}

void test_BigPacketPartially() {
  StreamDataPacketCollector collector;
  auto garbage = MakeStreamPacket(std::vector<std::uint8_t>(1200));
  // add 1 byte
  collector.AddData(garbage.data(), 1);
  {
    auto p = collector.PopPacket();
    TEST_ASSERT(p.empty());
  }
  collector.AddData(garbage.data() + 1, 1);
  {
    auto p = collector.PopPacket();
    TEST_ASSERT(p.empty());
  }
  // add rest of data
  collector.AddData(garbage.data() + 2, garbage.size() - 2);
  {
    // packet complete
    auto p = collector.PopPacket();
    TEST_ASSERT(!p.empty());
  }
}

struct GoldenSize {
  std::uint64_t value;
  std::uint8_t const* bytes;
  std::size_t size;
};

// Legacy Aether TieredInt wire golden vectors (LE), independent of codec.
static constexpr std::uint8_t k250[] = {0xfa};
static constexpr std::uint8_t k251[] = {0xfb, 0x00};
static constexpr std::uint8_t k1514[] = {0xff, 0xef};
static constexpr std::uint8_t k1515[] = {0xff, 0xf0, 0x00, 0x00};
static constexpr std::uint8_t k1049834[] = {0xff, 0xff, 0xff, 0xfe};
static constexpr std::uint8_t k1049835[] = {0xff, 0xff, 0x00, 0xff,
                                           0x00, 0x00, 0x00, 0x00};

static constexpr GoldenSize kGoldenSizes[] = {
    {250, k250, sizeof(k250)},
    {251, k251, sizeof(k251)},
    {1514, k1514, sizeof(k1514)},
    {1515, k1515, sizeof(k1515)},
    {1049834, k1049834, sizeof(k1049834)},
    {1049835, k1049835, sizeof(k1049835)},
};

std::vector<std::uint8_t> PayloadForSize(std::uint64_t size) {
  std::vector<std::uint8_t> payload(static_cast<std::size_t>(size));
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<std::uint8_t>(i & 0xffu);
  }
  return payload;
}

void AssertPayload(std::vector<std::uint8_t> const& got,
                   std::vector<std::uint8_t> const& expected) {
  TEST_ASSERT_EQUAL(expected.size(), got.size());
  if (!expected.empty()) {
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), got.data(), expected.size());
  }
}

void test_PackedSizeGoldenSerializeDeserialize() {
  for (GoldenSize const& g : kGoldenSizes) {
    std::vector<std::uint8_t> encoded;
    {
      auto buffer = VectorBuffer<PackedSize>{encoded};
      TEST_ASSERT(buffer.Write(seri::SizeWriteTag{static_cast<std::size_t>(g.value)}));
    }
    TEST_ASSERT_EQUAL(g.size, encoded.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g.bytes, encoded.data(), g.size);

    {
      auto buffer = VectorBuffer<PackedSize>{encoded};
      std::size_t decoded{};
      TEST_ASSERT(buffer.Read(seri::SizeReadTag{decoded}));
      TEST_ASSERT_EQUAL(static_cast<std::size_t>(g.value), decoded);
    }

    {
      auto archive =
          seri::BinaryArchive{seri::BinaryVectorBuffer<>{encoded}};
      PackedSize value{};
      TEST_ASSERT(archive.Load(value));
      TEST_ASSERT_EQUAL(static_cast<PackedSize::ValueType>(g.value),
                        static_cast<PackedSize::ValueType>(value));
    }

    {
      std::vector<std::uint8_t> roundtrip;
      auto archive =
          seri::BinaryArchive{seri::BinaryVectorBuffer<>{roundtrip}};
      TEST_ASSERT(archive.Save(PackedSize{g.value}));
      TEST_ASSERT_EQUAL(g.size, roundtrip.size());
      TEST_ASSERT_EQUAL_UINT8_ARRAY(g.bytes, roundtrip.data(), g.size);
    }
  }
}

void test_PacketFramingGoldenBoundaries() {
  for (GoldenSize const& g : kGoldenSizes) {
    auto payload = PayloadForSize(g.value);
    auto frame = MakeStreamPacket(payload);
    TEST_ASSERT_EQUAL(g.size + payload.size(), frame.size());
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g.bytes, frame.data(), g.size);

    StreamDataPacketCollector collector;
    collector.AddData(frame.data(), frame.size());
    auto got = collector.PopPacket();
    AssertPayload(got, payload);
    TEST_ASSERT(collector.PopPacket().empty());
  }
}

void FeedFragmented(StreamDataPacketCollector& collector,
                    std::uint8_t const* data, std::size_t size,
                    std::size_t chunk) {
  std::size_t offset = 0;
  while (offset < size) {
    auto const n = (offset + chunk <= size) ? chunk : (size - offset);
    collector.AddData(data + offset, n);
    offset += n;
  }
}

void test_FragmentedHeaderAfterEachByte() {
  for (GoldenSize const& g : kGoldenSizes) {
    auto payload = PayloadForSize(g.value);
    auto frame = MakeStreamPacket(payload);
    TEST_ASSERT_EQUAL(g.size, frame.size() - payload.size());

    StreamDataPacketCollector collector;
    // Split the size header after every possible byte, then deliver payload.
    for (std::size_t i = 0; i < g.size; ++i) {
      collector.AddData(frame.data() + i, 1);
      TEST_ASSERT(collector.PopPacket().empty());
    }
    if (!payload.empty()) {
      collector.AddData(frame.data() + g.size, payload.size());
    }
    AssertPayload(collector.PopPacket(), payload);
    TEST_ASSERT(collector.PopPacket().empty());
  }
}

void test_WholeHeaderThenPayload() {
  for (GoldenSize const& g : kGoldenSizes) {
    auto payload = PayloadForSize(g.value);
    auto frame = MakeStreamPacket(payload);

    StreamDataPacketCollector collector;
    collector.AddData(frame.data(), g.size);
    TEST_ASSERT(collector.PopPacket().empty());
    collector.AddData(frame.data() + g.size, payload.size());
    AssertPayload(collector.PopPacket(), payload);
  }
}

void test_MultipleFramesBackToBack() {
  std::vector<std::uint8_t> stream;
  std::vector<std::vector<std::uint8_t>> payloads;
  for (GoldenSize const& g : kGoldenSizes) {
    auto payload = PayloadForSize(g.value % 64u);  // keep multi-frame test light
    // Re-encode with the actual small payload size (not g.value).
    auto frame = MakeStreamPacket(payload);
    stream.insert(stream.end(), frame.begin(), frame.end());
    payloads.push_back(std::move(payload));
  }

  StreamDataPacketCollector collector;
  collector.AddData(stream.data(), stream.size());
  for (auto const& expected : payloads) {
    AssertPayload(collector.PopPacket(), expected);
  }
  TEST_ASSERT(collector.PopPacket().empty());
}

void test_TierHeaderLengthTransitions() {
  // Explicit 1 / 2 / 4 / 8-byte header transitions around tier boundaries.
  for (GoldenSize const& g : kGoldenSizes) {
    std::uint8_t buf[PackedSize::kMaxWireBytes]{};
    auto const n = PackedSize{g.value}.Serialize(buf);
    TEST_ASSERT_EQUAL(g.size, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(g.bytes, buf, g.size);

    // Frame a small payload but feed the whole frame one byte at a time so
    // collector state machines exercise header length transitions.
    auto payload = PayloadForSize(static_cast<std::uint64_t>(g.size));
    auto frame = MakeStreamPacket(payload);
    StreamDataPacketCollector collector;
    FeedFragmented(collector, frame.data(), frame.size(), 1);
    AssertPayload(collector.PopPacket(), payload);
  }
}

}  // namespace ae::test_data_pc

int test_data_packet_collector() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_data_pc::test_DataPacketCollectorEmpty);
  RUN_TEST(ae::test_data_pc::test_AddOnePacket);
  RUN_TEST(ae::test_data_pc::test_AddFewPackets);
  RUN_TEST(ae::test_data_pc::test_AddBigPacket);
  RUN_TEST(ae::test_data_pc::test_AddFewPacketInOne);
  RUN_TEST(ae::test_data_pc::test_BigPacketPartially);
  RUN_TEST(ae::test_data_pc::test_PackedSizeGoldenSerializeDeserialize);
  RUN_TEST(ae::test_data_pc::test_PacketFramingGoldenBoundaries);
  RUN_TEST(ae::test_data_pc::test_FragmentedHeaderAfterEachByte);
  RUN_TEST(ae::test_data_pc::test_WholeHeaderThenPayload);
  RUN_TEST(ae::test_data_pc::test_MultipleFramesBackToBack);
  RUN_TEST(ae::test_data_pc::test_TierHeaderLengthTransitions);
  return UNITY_END();
}
