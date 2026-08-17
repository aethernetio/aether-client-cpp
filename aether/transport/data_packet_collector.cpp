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

#include "aether/transport/data_packet_collector.h"

#include <cassert>
#include <cstring>

#include "aether/vector_buffer.h"

namespace ae {

Packet::Packet(std::size_t expected_size)
    : mem_buffer{std::make_unique<std::uint8_t[]>(expected_size)},
      expected_packet_size{expected_size},
      pos{} {}

Packet::Packet(Packet&& other) noexcept
    : mem_buffer{std::move(other.mem_buffer)},
      expected_packet_size{other.expected_packet_size},
      pos{other.pos} {}

void StreamDataPacketCollector::AddData(std::uint8_t const* data,
                                        std::size_t size) {
  std::size_t offset{};

  // write data to all packets
  while ((size - offset) > 0) {
    if (packets_.empty() || IsPacketComplete(packets_.back())) {
      auto [packet_size, ofst] = GetPacketSize(data + offset, size - offset);
      // no packet yet
      if (packet_size == 0) {
        return;
      }
      offset += ofst;
      packets_.emplace(packet_size);
    }

    offset += WriteToPacket(packets_.back(), data + offset, size - offset);
  }
}

std::vector<std::uint8_t> StreamDataPacketCollector::PopPacket() {
  // no completed packet, return empty
  if (packets_.empty() || !IsPacketComplete(packets_.front())) {
    return {};
  }
  auto& packet = packets_.front();
  std::vector<std::uint8_t> data_packet(packet.mem_buffer.get(),
                                        packet.mem_buffer.get() + packet.pos);

  packets_.pop();
  return data_packet;
}

bool StreamDataPacketCollector::IsPacketComplete(Packet const& packet) {
  return packet.pos == packet.expected_packet_size;
}

std::pair<std::size_t, std::size_t> StreamDataPacketCollector::GetPacketSize(
    std::uint8_t const* data, std::size_t size) {
  auto temp_buffer_size = temp_data_buffer_.size();

  // use no more than packet size may contain
  auto use_max_size = sizeof(PacketSize::ValueType) < size
                          ? sizeof(PacketSize::ValueType)
                          : size;

  temp_data_buffer_.insert(temp_data_buffer_.end(), data, data + use_max_size);

  VectorBuffer<PacketSize> vec_buffer(temp_data_buffer_);
  std::size_t packet_size{};

  if (!vec_buffer.Read(seri::SizeReadTag{packet_size})) {
    return {0, size};
  }

  temp_data_buffer_.clear();

  assert((temp_buffer_size + size) >= vec_buffer.read_offset);
  return {packet_size, vec_buffer.read_offset - temp_buffer_size};
}

std::size_t StreamDataPacketCollector::WriteToPacket(Packet& packet,
                                                     std::uint8_t const* data,
                                                     std::size_t size) {
  auto avail_cap = packet.expected_packet_size - packet.pos;
  auto write_size = avail_cap > size ? size : avail_cap;

  std::memcpy(packet.mem_buffer.get() + packet.pos, data, write_size);
  packet.pos += write_size;

  return write_size;
}

}  // namespace ae
