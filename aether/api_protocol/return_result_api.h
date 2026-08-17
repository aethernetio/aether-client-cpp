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

#include "aether-miscpp/reflect/reflect.h"

#include "aether/api_protocol/api_class_impl.h"
#include "aether/api_protocol/api_method.h"

namespace ae {
class ReturnResultApi : public ApiClass {
  template <typename T>
  struct SendResultMessage {
    AE_REFLECT_MEMBERS(req_id, data);
    RequestId req_id;
    T data;
  };

 public:
  static constexpr MessageId kSendResult = 0;
  static constexpr MessageId kSendError = 1;

  explicit ReturnResultApi(ProtocolContext& protocol_context);
  virtual ~ReturnResultApi() = default;

  void SendResultImpl(RequestId request_id);
  void SendErrorImpl(RequestId request_id, std::uint8_t error_type,
                     std::uint32_t error_code);

  template <typename T>
  void SendResult(RequestId req_id, T&& data) {
    auto* packet_stack = protocol_context().packet_stack();
    assert(packet_stack);
    packet_stack->Push(*this, SendResultMessage{req_id, std::forward<T>(data)});
  }

  void SendError(RequestId req_id, std::uint8_t error_type,
                 std::uint32_t error_code) {
    send_error_(req_id, error_type, error_code);
  }

  AE_METHODS(RegMethod<kSendResult, &ReturnResultApi::SendResultImpl>,
             RegMethod<kSendError, &ReturnResultApi::SendErrorImpl>);

  template <typename T>
  void Pack(SendResultMessage<T>&& result, ApiPacker& packer) const {
    packer.Pack(kSendResult, std::move(result));
  }

 private:
  Method<kSendError, void(RequestId req_id, std::uint8_t error_type,
                          std::uint32_t error_code)>
      send_error_;
};

}  // namespace ae

#endif  // AETHER_API_PROTOCOL_RETURN_RESULT_API_H_
