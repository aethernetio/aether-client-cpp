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

#include "aether/stream_api/safe_stream.h"

namespace ae {
SafeStreamApi::SafeStreamApi(ProtocolContext& protocol_context,
                             SafeStream& safe_stream)
    : ReturnResultApiImpl(protocol_context),
      close{protocol_context},
      request_report{protocol_context},
      put_report{protocol_context},
      confirm{protocol_context},
      request_repeat{protocol_context},
      send{protocol_context},
      repeat{protocol_context},
      protocol_context_{&protocol_context},
      safe_stream_{&safe_stream} {}

void SafeStreamApi::CloseImpl(ApiParser& /* parser */) {
  assert(false);  // NOT IMPLEMENTED
}

void SafeStreamApi::RequestReportImpl(ApiParser& /* parser */) {
  assert(false);  // NOT IMPLEMENTED
}

void SafeStreamApi::PutReportImpl(ApiParser& /* parser */,
                                  std::uint16_t /* offset */) {
  assert(false);  // NOT IMPLEMENTED
}

void SafeStreamApi::ConfirmImpl(ApiParser& /* parser */, std::uint16_t offset) {
  safe_stream_->Confirm(offset);
}

void SafeStreamApi::RequestRepeatImpl(ApiParser& /* parser */,
                                      std::uint16_t offset) {
  safe_stream_->RequestRepeat(offset);
}

void SafeStreamApi::SendImpl(ApiParser& /* parser */, std::uint16_t offset,
                             DataBuffer data) {
  safe_stream_->SendData(offset, std::move(data));
}

void SafeStreamApi::RepeatImpl(ApiParser& /* parser */,
                               std::uint16_t repeat_count, std::uint16_t offset,
                               DataBuffer data) {
  safe_stream_->RepeatData(repeat_count, offset, std::move(data));
}

}  // namespace ae
