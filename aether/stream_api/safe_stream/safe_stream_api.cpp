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

#include "aether/stream_api/safe_stream/safe_stream_api.h"

#include <cassert>

namespace ae {
SafeStreamApi::SafeStreamApi(ProtocolContext& protocol_context,
                             SafeStreamApiImpl& safe_stream_api_impl)
    : ApiClassImpl{protocol_context},
      ack{protocol_context},
      request_repeat{protocol_context},
      send{protocol_context},
      safe_stream_api_impl_{&safe_stream_api_impl} {}

void SafeStreamApi::AckImpl(SSRingIndex::type offset) {
  safe_stream_api_impl_->Ack(offset);
}

void SafeStreamApi::RequestRepeatImpl(SSRingIndex::type offset) {
  safe_stream_api_impl_->RequestRepeat(offset);
}

void SafeStreamApi::SendImpl(SSRingIndex::type begin_offset,
                             DataMessage data_message) {
  safe_stream_api_impl_->Send(begin_offset, std::move(data_message));
}

}  // namespace ae
