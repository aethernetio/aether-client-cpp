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

#include "aether/mstream.h"
#include "aether/mstream_buffers.h"
#include "aether/transport/data_packet_collector.h"

namespace ae::test_data_pc {
void test_DataPacketCollectorEmpty() {
  StreamDataPacketCollector collector;
  auto data_packet = collector.PopPacket();
  TEST_ASSERT(data_packet.empty());
}

std::vector<std::uint8_t> MakeStreamPacket(std::vector<std::uint8_t> data) {
  std::vector<std::uint8_t> packet_size_data;
  VectorWriter<PacketSize> writer(packet_size_data);
  omstream os{writer};
  os << PacketSize{data.size()};

  data.insert(data.begin(), packet_size_data.begin(), packet_size_data.end());

  return std::move(data);
}

std::vector<std::uint8_t> TestPacket() {
  std::vector<std::uint8_t> packet;
  VectorWriter<PacketSize> writer(packet);
  omstream os{writer};

  os << std::string{"Hello"};
  os << int{12};
  os << float{12.42};

  return MakeStreamPacket(std::move(packet));
}

inline void AssertPacket(std::vector<std::uint8_t> const& data_packet) {
  VectorReader<PacketSize> reader(data_packet);
  imstream is{reader};
  std::string str;
  int i;
  float f;
  is >> str >> i >> f;
  TEST_ASSERT_EQUAL_STRING("Hello", str.c_str());
  TEST_ASSERT_EQUAL(12, i);
  TEST_ASSERT_EQUAL(12.42, f);
}

void test_AddOnePacket() {
  StreamDataPacketCollector collector;
  auto packet = TestPacket();

  collector.AddData(packet);
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
    collector.AddData(std::move(packet));
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
  collector.AddData(garbage);

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

  collector.AddData(std::move(cumulative_packet));

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
  collector.AddData({garbage[0]});
  {
    auto p = collector.PopPacket();
    TEST_ASSERT(p.empty());
  }
  collector.AddData({garbage[1]});
  {
    auto p = collector.PopPacket();
    TEST_ASSERT(p.empty());
  }
  // add rest of data
  collector.AddData({std::next(std::begin(garbage), 2), std::end(garbage)});
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
