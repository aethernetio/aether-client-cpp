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

#ifndef AETHER_API_PROTOCOL_DETAILS_RETURN_RESULT_API_H_
#define AETHER_API_PROTOCOL_DETAILS_RETURN_RESULT_API_H_

#include <cstdint>

#include "aether-miscpp/reflect/reflect.h"

#include "aether/api_protocol/details/api_internal_context.h"
// IWYU pragma: begin_keeps
#include "aether/api_protocol/details/packet_list.h"
#include "aether/api_protocol/details/protocol_context.h"
#include "aether/api_protocol/details/request_id.h"
// IWYU pragma: end_keeps

namespace ae {
namespace return_result_api_internal {
struct SendResultTag {};
struct SendErrorTag {};
static inline constexpr auto send_result_tag = SendResultTag{};
static inline constexpr auto send_error_tag = SendErrorTag{};
}  // namespace return_result_api_internal

namespace api_class_declate_internal {
template <MessageId, auto method>
struct RegMethod;

template <MessageId id>
struct RegMethod<id, return_result_api_internal::send_result_tag> {
  static constexpr auto kMessageId = id;
  static constexpr auto kMethod = return_result_api_internal::send_result_tag;
};

template <MessageId id>
struct RegMethod<id, return_result_api_internal::send_error_tag> {
  static constexpr auto kMessageId = id;
  static constexpr auto kMethod = return_result_api_internal::send_error_tag;
};

}  // namespace api_class_declate_internal

class ReturnResultApi {
  template <typename T>
  struct SendResultMessage {
    AE_REFLECT_MEMBERS(req_id, data);
    RequestId req_id;
    T data;
  };

  struct SendErrorMessage {
    AE_REFLECT_MEMBERS(req_id, error_type, error_code);
    RequestId req_id;
    std::uint8_t error_type;
    std::int32_t error_code;
  };

 public:
  static constexpr MessageId kSendResult = 0;
  static constexpr MessageId kSendError = 1;

  using RegSendResult = api_class_declate_internal::RegMethod<
      kSendResult, return_result_api_internal::send_result_tag>;
  using RegSendError = api_class_declate_internal::RegMethod<
      kSendError, return_result_api_internal::send_error_tag>;

  template <typename T>
  static void SendResult(ClientApiContext& client_context, RequestId req_id,
                         T&& data) {
    using M = SendResultMessage<std::decay_t<T>>;
    client_context.packet_list().Push(ApiMessage<kSendResult, M>{
        M{.req_id = req_id, .data = std::forward<T>(data)}});
  }

  static void SendError(ClientApiContext& client_context, RequestId req_id,
                        std::uint8_t error_type, std::int32_t error_code) {
    client_context.packet_list().Push(
        ApiMessage<kSendError, SendErrorMessage>{SendErrorMessage{
            .req_id = req_id,
            .error_type = error_type,
            .error_code = error_code,
        }});
  }

  static void SendResultImpl(ServerApiContext& server_context) {
    auto& reader = server_context.reader();
    auto request_id = reader.TryExtract<RequestId>();
    if (!request_id) {
      reader.Cancel();
      return;
    }
    if (!server_context.protocol_context().SetSendResultResponse(reader,
                                                                 *request_id)) {
      reader.Cancel();
    }
  }

  static void SendErrorImpl(ServerApiContext& server_context) {
    auto& reader = server_context.reader();
    auto request_id = reader.TryExtract<RequestId>();
    if (!request_id) {
      reader.Cancel();
      return;
    }
    auto error_type = reader.TryExtract<std::uint8_t>();
    if (!error_type) {
      reader.Cancel();
      return;
    }
    auto error_code = reader.TryExtract<std::int32_t>();
    if (!error_code) {
      reader.Cancel();
      return;
    }

    if (!server_context.protocol_context().SetSendErrorResponse(
            *request_id, *error_type, *error_code)) {
      server_context.reader().Cancel();
    }
  }

  static consteval auto ServeApiList() {
    return TypeList<RegSendResult, RegSendError>{};
  }
};

}  // namespace ae

#endif  // AETHER_API_PROTOCOL_DETAILS_RETURN_RESULT_API_H_
