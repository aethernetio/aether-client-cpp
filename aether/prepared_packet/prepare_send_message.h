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

#ifndef AETHER_PREPARED_PACKET_PREPARE_SEND_MESSAGE_H_
#define AETHER_PREPARED_PACKET_PREPARE_SEND_MESSAGE_H_

#include <cstdint>
#include <optional>

#include "aether/prepared_packet/prepared_send_message.h"

namespace ae {

class P2pStream;

namespace prepared_packet {

std::optional<PreparedSendMessageBlock> PrepareSendMessage(
    P2pStream& stream, std::uint32_t reserve_nonce_count);

}  // namespace prepared_packet
}  // namespace ae

#endif  // AETHER_PREPARED_PACKET_PREPARE_SEND_MESSAGE_H_
