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

#include <ios>
#include <algorithm>
#include <cassert>

#include "aether/mstream_buffers.h"
#include "aether/mstream.h"

namespace ae {

Packet::Packet(std::size_t expected_size)
    : mem_buffer{static_cast<std::streamsize>(expected_size)},
      expected_packet_size{expected_size} {}

Packet::Packet(Packet&& other) noexcept
    : mem_buffer{std::move(other.mem_buffer)},
      expected_packet_size{other.expected_packet_size} {}

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

void StreamDataPacketCollector::AddData(DataBuffer data_buffer) {
  AddData(data_buffer.data(), data_buffer.size());
}

std::vector<std::uint8_t> StreamDataPacketCollector::PopPacket() {
  // no completed packet, return empty
  if (packets_.empty() || !IsPacketComplete(packets_.front())) {
    return {};
  }
  auto& packet = packets_.front();
  std::vector<std::uint8_t> data_packet(packet.expected_packet_size);
  std::copy(packet.mem_buffer.Data(),
            packet.mem_buffer.Data() + packet.mem_buffer.Size(),
            std::begin(data_packet));

  packets_.pop();
  return data_packet;
}

bool StreamDataPacketCollector::IsPacketComplete(Packet const& packet) {
  return packet.mem_buffer.Size() ==
         static_cast<std::streamsize>(packet.expected_packet_size);
}

std::pair<std::size_t, std::size_t> StreamDataPacketCollector::GetPacketSize(
    std::uint8_t const* data, std::size_t size) {
  auto temp_buffer_size = temp_data_buffer_.size();

  // use no more than packet size may contain
  auto use_max_size = sizeof(PacketSize::ValueType) < size
                          ? sizeof(PacketSize::ValueType)
                          : size;

  temp_data_buffer_.insert(temp_data_buffer_.end(), data, data + use_max_size);

  VectorReader<PacketSize> reader(temp_data_buffer_);
  auto is = imstream{reader};

  PacketSize packet_size;
  is >> packet_size;
  if (!data_was_read(is)) {
    return {0, size};
  }

  temp_data_buffer_.clear();

  assert((temp_buffer_size + size) >= reader.offset_);
  return {static_cast<std::size_t>(packet_size),
          reader.offset_ - temp_buffer_size};
}

std::size_t StreamDataPacketCollector::WriteToPacket(Packet& packet,
                                                     std::uint8_t const* data,
                                                     std::size_t size) {
  auto avail_cap = packet.mem_buffer.AvailableCapacity();
  auto write_size = avail_cap > static_cast<std::streamsize>(size)
                        ? static_cast<std::streamsize>(size)
                        : avail_cap;

  packet.mem_buffer.sputn(reinterpret_cast<char const*>(data), write_size);

  return static_cast<std::size_t>(write_size);
}

}  // namespace ae
