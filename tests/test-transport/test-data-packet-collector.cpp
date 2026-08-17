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
#include <string>
#include <vector>

#include "aether-miscpp/serialization/binary_archive.h"

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
}  // namespace ae::test_data_pc

int test_data_packet_collector() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_data_pc::test_DataPacketCollectorEmpty);
  RUN_TEST(ae::test_data_pc::test_AddOnePacket);
  RUN_TEST(ae::test_data_pc::test_AddFewPackets);
  RUN_TEST(ae::test_data_pc::test_AddBigPacket);
  RUN_TEST(ae::test_data_pc::test_AddFewPacketInOne);
  RUN_TEST(ae::test_data_pc::test_BigPacketPartially);
  return UNITY_END();
}
