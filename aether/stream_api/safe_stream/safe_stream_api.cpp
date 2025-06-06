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
    : ReturnResultApiImpl(protocol_context),
      init{protocol_context},
      init_ack{protocol_context},
      confirm{protocol_context},
      request_repeat{protocol_context},
      send{protocol_context},
      repeat{protocol_context},
      safe_stream_api_impl_{&safe_stream_api_impl} {}

void SafeStreamApi::InitImpl(ApiParser& /* parser */, RequestId req_id,
                             std::uint16_t repeat_count,
                             SafeStreamInit safe_stream_init) {
  safe_stream_api_impl_->Init(req_id, repeat_count, safe_stream_init);
}

void SafeStreamApi::InitAckImpl(ApiParser& /* parser */, RequestId req_id,
                                SafeStreamInit safe_stream_init) {
  safe_stream_api_impl_->InitAck(req_id, safe_stream_init);
}

void SafeStreamApi::ConfirmImpl(ApiParser& /* parser */, std::uint16_t offset) {
  safe_stream_api_impl_->Confirm(offset);
}

void SafeStreamApi::RequestRepeatImpl(ApiParser& /* parser */,
                                      std::uint16_t offset) {
  safe_stream_api_impl_->RequestRepeat(offset);
}

void SafeStreamApi::SendImpl(ApiParser& /* parser */, std::uint16_t offset,
                             DataBuffer data) {
  safe_stream_api_impl_->Send(offset, std::move(data));
}

void SafeStreamApi::RepeatImpl(ApiParser& /* parser */,
                               std::uint16_t repeat_count, std::uint16_t offset,
                               DataBuffer data) {
  safe_stream_api_impl_->Repeat(repeat_count, offset, std::move(data));
}

}  // namespace ae
