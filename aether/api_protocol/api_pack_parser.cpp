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

#include "aether/api_protocol/api_pack_parser.h"

#include "aether/api_protocol/child_data.h"

namespace ae {
ApiParser::ApiParser(ProtocolContext& protocol_context,
                     std::vector<std::uint8_t> const& data)
    : protocol_context_{protocol_context}, buffer_reader_{data, *this} {
  protocol_context_.PushParser(*this);
}

ApiParser::ApiParser(ProtocolContext& protocol_context_,
                     ChildData const& child_data)
    : ApiParser{protocol_context_, child_data.PackData()} {}

ApiParser::~ApiParser() { protocol_context_.PopParser(); }

ProtocolContext& ApiParser::Context() { return protocol_context_; }

void ApiParser::Cancel() {
  buffer_reader_.offset_ = buffer_reader_.data_.size();
}

ApiPacker::ApiPacker(ProtocolContext& protocol_context_,
                     std::vector<std::uint8_t>& data)
    : protocol_context_{protocol_context_}, buffer_writer_{data, *this} {
  protocol_context_.PushPacker(*this);
}

ApiPacker::~ApiPacker() { protocol_context_.PopPacker(); }

MessageBufferWriter& ApiPacker::Buffer() { return buffer_writer_; }

ProtocolContext& ApiPacker::Context() { return protocol_context_; }
}  // namespace ae
