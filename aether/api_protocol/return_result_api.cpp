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

#include "aether/api_protocol/return_result_api.h"

namespace ae {
ReturnResultApi::ReturnResultApi(ProtocolContext& protocol_context)
    : ApiClass{protocol_context}, send_error_{protocol_context} {}

void ReturnResultApi::SendResultImpl(ApiParser& parser, RequestId request_id) {
  protocol_context().SetSendResultResponse(request_id, parser);
}

void ReturnResultApi::SendErrorImpl(ApiParser& parser, RequestId request_id,
                                    std::uint8_t error_type,
                                    std::uint32_t error_code) {
  protocol_context().SetSendErrorResponse(request_id, error_type, error_code,
                                          parser);
}

void ReturnResultApi::Pack(ae::SendResult&& result, ApiPacker& packer) {
  packer.Pack(kSendResult, std::move(result));
}

}  // namespace ae
