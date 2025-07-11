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

#ifndef AETHER_API_PROTOCOL_RETURN_RESULT_API_H_
#define AETHER_API_PROTOCOL_RETURN_RESULT_API_H_

#include <cstdint>

#include "aether/api_protocol/api_class_impl.h"
#include "aether/api_protocol/send_result.h"

namespace ae {
class ReturnResultApiImpl {
 public:
  static constexpr MessageId kSendResult = 0;
  static constexpr MessageId kSendError = 1;

  explicit ReturnResultApiImpl(ProtocolContext& protocol_context);
  virtual ~ReturnResultApiImpl() = default;

  void SendResultImpl(ApiParser& parser, RequestId request_id);
  void SendErrorImpl(ApiParser& parser, RequestId request_id,
                     std::uint8_t error_type, std::uint32_t error_code);

  template <typename T>
  void SendResult(RequestId req_id, T&& data) {
    auto* packet_stack = protocol_context_->packet_stack();
    assert(packet_stack);
    packet_stack->Push(ReturnResultApi{},
                       ::ae::SendResult{req_id, std::move(data)});
  }

  void SendError(RequestId req_id, std::uint8_t error_type,
                 std::uint32_t error_code) {
    send_error_(req_id, error_type, error_code);
  }

  using ApiMethods =
      ImplList<RegMethod<kSendResult, &ReturnResultApiImpl::SendResultImpl>,
               RegMethod<kSendError, &ReturnResultApiImpl::SendErrorImpl>>;

 private:
  ProtocolContext* protocol_context_;

  Method<kSendResult, void(RequestId req_id, std::uint8_t error_type,
                           std::uint32_t error_code)>
      send_error_;
};

}  // namespace ae

#endif  // AETHER_API_PROTOCOL_RETURN_RESULT_API_H_
