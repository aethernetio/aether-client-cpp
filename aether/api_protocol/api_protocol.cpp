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

#include "aether/api_protocol/api_protocol.h"

#include <limits>
#include <iostream>

#include "aether/api_protocol/child_data.h"

namespace ae {
ApiParser::ApiParser(ProtocolContext& protocol_context,
                     std::vector<std::uint8_t> const& data)
    : protocol_context_{protocol_context}, buffer_reader_{data, *this} {}

ApiParser::ApiParser(ProtocolContext& protocol_context_,
                     ChildData const& child_data)
    : ApiParser{protocol_context_, child_data.PackData()} {}

ApiParser::~ApiParser() = default;

void ApiParser::Parse(ApiClass& api_class) {
  while (buffer_reader_.offset_ < buffer_reader_.data_.size()) {
    MessageId message_id{std::numeric_limits<MessageId>::max()};
    istream_ >> message_id;
    api_class.LoadFactory(message_id, *this);
  }
}

ProtocolContext& ApiParser::Context() { return protocol_context_; }

void ApiParser::Cancel() {
  buffer_reader_.offset_ = buffer_reader_.data_.size();
}

ApiPacker::ApiPacker(ProtocolContext& protocol_context_,
                     std::vector<std::uint8_t>& data)
    : protocol_context_{protocol_context_}, buffer_writer_{data, *this} {}

ApiPacker::~ApiPacker() = default;

MessageBufferWriter& ApiPacker::Buffer() { return buffer_writer_; }

ProtocolContext& ApiPacker::Context() { return protocol_context_; }

bool ReturnResultApi::LoadResult(MessageId message_id, ApiParser& parser) {
  switch (message_id) {
    case kSendResult:
      parser.Load<SendResult>(*this);
      return true;
    case kSendError:
      parser.Load<SendError>(*this);
      return true;
    default:
      break;
  }
  return false;
}

void ReturnResultApi::Execute(SendResult&& result, ApiParser& parser) {
  parser.Context().SetSendResultResponse(result.request_id, parser);
}

void ReturnResultApi::Execute(SendError&& error, ApiParser& parser) {
  std::cerr << "SendError: id " << error.request_id.id
            << " type: " << static_cast<int>(error.error_type)
            << " error code: " << error.error_code << std::endl;
  parser.Context().MessageNotify(std::move(error));
}

void ReturnResultApi::Pack(SendResult&& result, ApiPacker& packer) const {
  packer.Pack(kSendResult, std::move(result));
}

void ReturnResultApi::Pack(SendError&& error, ApiPacker& packer) const {
  packer.Pack(kSendError, std::move(error));
}

}  // namespace ae
