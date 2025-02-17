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

#ifndef AETHER_API_PROTOCOL_SEND_RESULT_H_
#define AETHER_API_PROTOCOL_SEND_RESULT_H_

#include <vector>
#include <utility>
#include <cstdint>

#include "aether/mstream.h"
#include "aether/mstream_buffers.h"
#include "aether/api_protocol/request_id.h"
#include "aether/api_protocol/api_message.h"
#include "aether/api_protocol/protocol_context.h"

namespace ae {
struct SendResult : public Message<SendResult> {
  static constexpr std::uint32_t kMessageId = 0;

  SendResult() = default;

  // Construct from T data
  template <typename T>
  SendResult(RequestId request_id, T const& t)
      : request_id{std::move(request_id)} {
    VectorWriter<PackedSize> mbw{child_data};
    auto os = omstream{mbw};
    os << t;
  }

  // convert to T data
  template <typename T>
  auto Get() const {
    VectorReader<PackedSize> mbr{child_data};
    auto is = imstream{mbr};
    T t{};
    is >> t;
    return t;
  }

  template <typename CbFunc>
  static void OnResponse(ProtocolContext& context, RequestId req_id,
                         CbFunc&& cb) {
    context.AddSendResultCallback(req_id, std::forward<CbFunc>(cb));
  }

  template <typename Ib>
  friend imstream<Ib>& operator>>(imstream<Ib>& is, SendResult& sr) {
    is >> sr.request_id;
    // child data will be parsed from the stream
    return is;
  }

  template <typename Ob>
  friend omstream<Ob>& operator<<(omstream<Ob>& os, SendResult const& sr) {
    os << sr.request_id;
    // write data as is
    os.write(sr.child_data.data(), sr.child_data.size());
    return os;
  }

  RequestId request_id;
  // for serialize only
  std::vector<std::uint8_t> child_data;
};

struct SendError : public Message<SendError> {
  static constexpr std::uint32_t kMessageId = 1;

  template <typename T>
  void Serializator(T& s) {
    s & request_id & error_type & error_code;
  }

  RequestId request_id;
  std::uint8_t error_type;
  std::uint32_t error_code;
};
}  // namespace ae

#endif  // AETHER_API_PROTOCOL_SEND_RESULT_H_ */
