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

#ifndef AETHER_API_PROTOCOL_DETAILS_API_PARSER_H_
#define AETHER_API_PROTOCOL_DETAILS_API_PARSER_H_

#include <cassert>
#include <limits>

#include "aether-miscpp/misc/defer.h"

#include "aether/types/data_buffer.h"

#include "aether/api_protocol/details/api_internal_context.h"
#include "aether/api_protocol/details/api_message.h"
#include "aether/api_protocol/details/packet_reader.h"

namespace ae {
class ProtocolContext;
// Parsing raw data buffer to API messages
class ApiParser {
 public:
  class ServerApiContextImpl final : public ServerApiContext {
   public:
    ServerApiContextImpl(ProtocolContext& pc, DataBuffer const& buffer);
    PacketReader& reader() override;
    ProtocolContext& protocol_context() override;

   private:
    ProtocolContext& protocol_context_;
    PacketReader reader_;
  };

  ApiParser(ProtocolContext& protocol_context_, DataBuffer const& data);

  /**
   * \brief Parse api class
   */
  template <typename Api>
  bool Parse(Api& api_class) {
    api_class.set_server_context(server_context_);
    ae_defer[&] { api_class.reset_server_context(); };
    auto& reader = server_context_.reader();

    while (!reader.canceled() && !reader.eof()) {
      auto msg_id = reader.TryExtract<MessageId>();
      if (!msg_id) {
        reader.Cancel();
        assert(false && "message_id didn't loaded");
        return false;
      }
      if (!api_class.LoadMethod(*msg_id)) {
        return false;
      }
    }
    return reader.eof() && !reader.canceled();
  }

 private:
  ServerApiContextImpl server_context_;
};

}  // namespace ae

#endif  // AETHER_API_PROTOCOL_DETAILS_API_PARSER_H_
