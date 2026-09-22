/*
 * Copyright 2026 Aethernet Inc.
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

#include "aether/api_protocol/details/packet_reader.h"

namespace ae {
PacketReader::PacketReader(DataBuffer const& buffer)
    : archive{MessageBuffer{
          const_cast<DataBuffer&>(buffer)  // NOLINT(*const-cast*)
      }} {}

void PacketReader::Cancel() { canceled_ = true; }

bool PacketReader::canceled() const { return canceled_; }

bool PacketReader::eof() const {
  return archive.buffer().read_offset == archive.buffer().buff.size();
}

}  // namespace ae
