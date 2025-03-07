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

#ifndef TESTS_TEST_API_PROTOCOL_API_WITH_RESULT_H_
#define TESTS_TEST_API_PROTOCOL_API_WITH_RESULT_H_

#include <string>
#include <utility>

#include "aether/api_protocol/api_protocol.h"
#include "aether/api_protocol/send_result.h"

namespace ae {
class ApiWithResult : public ApiClass, public ReturnResultApi {
 public:
  static constexpr auto kClassId = 3;

  struct RequestEcho : public Message<RequestEcho> {
    static constexpr std::uint32_t kMessageId = 12;

    AE_REFLECT_MEMBERS(id_, message_)

    RequestId id_;
    std::string message_;
  };

  // not sending. converted to/from send result response
  struct ResponseEcho : public Message<ResponseEcho> {
    static constexpr std::uint32_t kMessageId = 13;

    RequestId id_;
    std::string message_;
  };

  void LoadFactory(MessageId message_id, ApiParser &parser) override {
    if (ReturnResultApi::LoadResult(message_id, parser)) {
      return;
    }
    switch (message_id) {
      case 2:
        parser.Load<RequestEcho>(*this);
        break;
      default:
        assert(false);
        break;
    }
  }

  void Execute(RequestEcho &&request, ApiParser &parser) {
    parser.Context().MessageNotify(std::move(request));
  }

  void Pack(RequestEcho &&request, ApiPacker &packer) {
    SendResult::OnResponse(
        packer.Context(), request.id_,
        [req_id{request.id_}, context{&packer.Context()}](ApiParser &parser) {
          context->MessageNotify(
              ResponseEcho{{}, req_id, parser.Extract<std::string>()});
        });

    packer.Pack(2, std::move(request));
  }

  void Pack(ResponseEcho &&response, ApiPacker &packer) {
    auto send_result = SendResult{response.id_, response.message_};
    ReturnResultApi::Pack(std::move(send_result), packer);
  }
};

}  // namespace ae

#endif  // TESTS_TEST_API_PROTOCOL_API_WITH_RESULT_H_ */
