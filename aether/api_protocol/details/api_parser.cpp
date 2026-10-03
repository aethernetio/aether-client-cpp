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

#include "aether/api_protocol/details/api_parser.h"

namespace ae {
ApiParser::ServerApiContextImpl::ServerApiContextImpl(ProtocolContext& pc,
                                                      DataBuffer const& buffer)
    : protocol_context_{pc}, reader_{buffer} {}

PacketReader& ApiParser::ServerApiContextImpl::reader() { return reader_; }
ProtocolContext& ApiParser::ServerApiContextImpl::protocol_context() {
  return protocol_context_;
}

ApiParser::ApiParser(ProtocolContext& protocol_context, DataBuffer const& data)
    : server_context_{protocol_context, data} {}
}  // namespace ae
