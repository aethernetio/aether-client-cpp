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

#include "aether/stream_api/sized_packet_gate.h"

#include <utility>

#include "aether/vector_buffer.h"

namespace ae {

static constexpr std::size_t kSizedPacketOverhead =
    PacketSize::kMaxWireBytes;  // max for packet size

DataBuffer SizedPacketGate::WriteIn(DataBuffer&& buffer) {
  DataBuffer write_buffer;
  write_buffer.reserve(buffer.size() + kSizedPacketOverhead);
  auto vec_buff = VectorBuffer<PacketSize>{write_buffer};
  vec_buff.Write(seri::SizeWriteTag{buffer.size()});
  vec_buff.Write(
      seri::DataWriteTag{std::move(buffer).data(), std::move(buffer).size()});

  return write_buffer;
}

void SizedPacketGate::WriteOut(DataBuffer const& buffer) {
  data_packet_collector_.AddData(buffer.data(), buffer.size());

  for (auto packet = data_packet_collector_.PopPacket(); !packet.empty();
       packet = data_packet_collector_.PopPacket()) {
    out_data_event_.Emit(packet);
  }
}

std::size_t SizedPacketGate::Overhead() const { return kSizedPacketOverhead; }

EventSubscriber<void(DataBuffer const& data)>
SizedPacketGate::out_data_event() {
  return EventSubscriber{out_data_event_};
}
}  // namespace ae
