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

#ifndef AETHER_CLIENT_MESSAGES_P2P_STREAM_WRITE_BUDGET_H_
#define AETHER_CLIENT_MESSAGES_P2P_STREAM_WRITE_BUDGET_H_

#include <cstddef>

namespace ae {

// channel StreamInfo::max_element_size is the authorized datagram budget on the
// wire (e.g. UDP payload). P2pStream::Write accepts the inner payload that
// becomes AeMessage::data before LoginByAlias/SendMessage/crypto.
std::size_t MaxP2pStreamWritePayloadBytesForDatagramBudget(
    std::size_t datagram_budget_bytes);

std::size_t EncodedP2pSendMessageDatagramBytesForInnerPayload(
    std::size_t inner_payload_bytes);

}  // namespace ae

#endif  // AETHER_CLIENT_MESSAGES_P2P_STREAM_WRITE_BUDGET_H_
