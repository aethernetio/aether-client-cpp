/*
 * Copyright 2025 Aethernet Inc.
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

#if AE_SUPPORT_LORA

#  include "aether/lora_modules/api/lora_module_api.h"
#  include "aether/crc/crc32.h"

namespace ae {
LoraModuleApi::LoraModuleApi(ProtocolContext& protocol_context)
    : ApiClassImpl{protocol_context}, lora_packet{protocol_context} {}

void LoraModuleApi::LoraPacketImpl(ApiParser& parser, LoraConnection lc,
                                   DataBuffer data, std::uint32_t crc) {
  if (crc == CRC32_function(data)) {
    lora_packet_event_.Emit(lc, data, crc);
  }
}

EventSubscriber<void(LoraConnection const& lc, DataBuffer const& data,
                     std::uint32_t crc)>
lora_packet_event() {
  return EventSubscriber{lora_packet_event_};
}
}  // namespace ae

#endif  // AE_SUPPORT_LORA
