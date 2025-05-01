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

#ifndef AETHER_STREAM_API_PROTOCOL_GATES_H_
#define AETHER_STREAM_API_PROTOCOL_GATES_H_

#include <utility>

#include "aether/stream_api/istream.h"
#include "aether/api_protocol/api_context.h"
#include "aether/api_protocol/protocol_context.h"

namespace ae {
/**
 * \brief Parses read buffer as TApiClass
 */
template <typename TApiClass>
class ProtocolReadGate {
 public:
  template <typename TApi>
  ProtocolReadGate(ProtocolContext& protocol_context, TApi&& api_class)
      : protocol_context_{protocol_context},
        api_class_{std::forward<TApi>(api_class)} {}

  void WriteOut(DataBuffer const& buffer) {
    auto api_parser = ApiParser{protocol_context_, buffer};
    api_parser.Parse(api_class_);
  }

 private:
  ProtocolContext& protocol_context_;
  TApiClass api_class_;
};

template <typename TApi>
ProtocolReadGate(ProtocolContext& protocol_context, TApi&& api_class)
    -> ProtocolReadGate<TApi>;

/**
 * \brief Api method call adapter to automatically flush the packet after all
 * method calls.
 */
template <typename TApi>
class ApiCallAdapter {
 public:
  ApiCallAdapter(ApiContext<TApi>&& api_context, ByteIStream& byte_stream)
      : api_context_{std::move(api_context)}, byte_stream_{&byte_stream} {}

  AE_CLASS_MOVE_ONLY(ApiCallAdapter)

  ActionView<StreamWriteAction> Flush() {
    return byte_stream_->Write(std::move(api_context_));
  }

  ApiContext<TApi>& operator->() { return api_context_; }

 private:
  ApiContext<TApi> api_context_;
  ByteIStream* byte_stream_;
};

}  // namespace ae

#endif  // AETHER_STREAM_API_PROTOCOL_GATES_H_
