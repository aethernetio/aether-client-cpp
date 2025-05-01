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

#ifndef AETHER_STREAM_API_SIZED_PACKET_GATE_H_
#define AETHER_STREAM_API_SIZED_PACKET_GATE_H_

#include "aether/stream_api/istream.h"
#include "aether/transport/low_level/tcp/data_packet_collector.h"

namespace ae {
class SizedPacketGate {
 public:
  DataBuffer WriteIn(DataBuffer&& buffer);
  void WriteOut(DataBuffer const& buffer);
  std::size_t Overhead() const;
  EventSubscriber<void(DataBuffer const& data)> out_data_event();

 private:
  void DataReceived(DataBuffer const& buffer);

  StreamDataPacketCollector data_packet_collector_;
  Event<void(DataBuffer const& data)> out_data_event_;
};
}  // namespace ae
#endif  // AETHER_STREAM_API_SIZED_PACKET_GATE_H_
