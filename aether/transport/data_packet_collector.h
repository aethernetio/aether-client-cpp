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

#ifndef AETHER_TRANSPORT_DATA_PACKET_COLLECTOR_H_
#define AETHER_TRANSPORT_DATA_PACKET_COLLECTOR_H_

#include <cstddef>
#include <cstdint>
#include <vector>
#include <queue>
#include <utility>

#include "third_party/aethernet-numeric/numeric/tiered_int.h"

#include "aether/memory_buffer.h"
#include "aether/types/data_buffer.h"

namespace ae {

using PacketSize = TieredInt<std::uint64_t, std::uint8_t, 250>;

struct Packet {
  explicit Packet(std::size_t expected_size);
  Packet(Packet const&) = delete;
  Packet(Packet&& other) noexcept;

  MemStreamBuf<> mem_buffer;
  std::size_t expected_packet_size;
};

class StreamDataPacketCollector {
 public:
  // fill packets in queue with provided stream data_buffer
  void AddData(std::uint8_t const* data, std::size_t size);
  void AddData(DataBuffer data_buffer);
  // pops a packet data if any, else return empty
  std::vector<std::uint8_t> PopPacket();

 private:
  static bool IsPacketComplete(Packet const& packet);
  // return new packet size and data offset to start reading
  std::pair<std::size_t, std::size_t> GetPacketSize(std::uint8_t const* data,
                                                    std::size_t size);
  // return data offset
  std::size_t WriteToPacket(Packet& packet, std::uint8_t const* data,
                            std::size_t size);

  std::queue<Packet> packets_;
  // used if packet is not complete to get packet size
  std::vector<std::uint8_t> temp_data_buffer_;
};
}  // namespace ae

#endif  // AETHER_TRANSPORT_DATA_PACKET_COLLECTOR_H_
