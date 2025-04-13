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

#ifndef AETHER_API_PROTOCOL_API_PROTOCOL_H_
#define AETHER_API_PROTOCOL_API_PROTOCOL_H_

#include <tuple>
#include <vector>
#include <utility>
#include <cassert>
#include <cstdint>

#include "aether/api_protocol/protocol_context.h"
#include "aether/api_protocol/api_message.h"
#include "aether/api_protocol/send_result.h"

namespace ae {
class ChildData;

class ApiParser;
class ApiPacker;

class ApiClass {
 public:
  virtual ~ApiClass() = default;

  virtual void LoadFactory(MessageId /* message_id */,
                           ApiParser& /* parser */) {
    assert(false);
  }
};

// Parsing raw data buffer to API messages
class ApiParser {
 public:
  ApiParser(ProtocolContext& protocol_context_,
            std::vector<std::uint8_t> const& data);
  ApiParser(ProtocolContext& protocol_context_, ChildData const& child_data);
  ~ApiParser();

  void Parse(ApiClass& api_class);

  template <typename Message, typename TApiClass>
  void Load(TApiClass& api_class) {
    Message msg{};
    msg.Load(istream_);
    protocol_context_.PushApiClass(TApiClass::kClassId, &api_class);
    api_class.Execute(std::move(msg), *this);
    protocol_context_.PopApiClass(TApiClass::kClassId);
  }

  template <typename T>
  T Extract() {
    T result{};
    istream_ >> result;
    return result;
  }

  // cancel parsing
  void Cancel();
  ProtocolContext& Context();

 private:
  ProtocolContext& protocol_context_;
  MessageBufferReader buffer_reader_;
  message_istream istream_{buffer_reader_};
};

// Packing API messages to raw data buffer
class ApiPacker {
 public:
  ApiPacker(ProtocolContext& protocol_context_,
            std::vector<std::uint8_t>& data);
  ~ApiPacker();

  template <typename Message>
  void Pack(MessageId message_id, Message&& msg) {
    ostream_ << message_id;
    std::forward<Message>(msg).Save(ostream_);
  }

  MessageBufferWriter& Buffer();

  ProtocolContext& Context();

 private:
  ProtocolContext& protocol_context_;
  MessageBufferWriter buffer_writer_;
  message_ostream ostream_{buffer_writer_};
};

/**
 * \brief An API class that extends derived API class with specified APIS
 */
template <typename... TApis>
class ExtendsApi {
 public:
  ExtendsApi() : apis_{} {}

  bool LoadExtend(MessageId message_id, ApiParser& parser) {
    return std::apply(
        [&](auto&... api) {
          return (LoadApiImpl(api, message_id, parser) || ...);
        },
        apis_);
  }

  template <typename T>
  void Pack(T&& message, ApiPacker& packer) {
    std::apply(
        [&](auto&... api) {
          (PackImpl<std::decay_t<T>, decltype(api)>::Pack(
               api, std::forward<T>(message), packer),
           ...);
        },
        apis_);
  }

 private:
  template <typename T>
  bool LoadApiImpl(T& api, MessageId message_id, ApiParser& parser) {
    return api.LoadResult(message_id, parser);
  }

  template <typename T, typename TApi, typename _ = void>
  struct PackImpl {
    static void Pack(TApi& /* api */, T&& /* message */,
                     ApiPacker& /* packer */) {}
  };

  template <typename T, typename TApi>
  struct PackImpl<T, TApi,
                  std::void_t<decltype(std::declval<TApi&>().Pack(
                      std::declval<T&&>(), std::declval<ApiPacker&>()))>> {
    static void Pack(TApi& api, T&& message, ApiPacker& packer) {
      api.Pack(std::forward<T>(message), packer);
    }
  };

  std::tuple<TApis...> apis_;
};

class ReturnResultApi {
 public:
  static constexpr auto kClassId = 0;

  static constexpr MessageId kSendResult = 0;
  static constexpr MessageId kSendError = 1;

  bool LoadResult(MessageId message_id, ApiParser& parser);

  void Execute(SendResult&& result, ApiParser& parser);
  void Execute(SendError&& error, ApiParser& parser);

  void Pack(SendResult&& result, ApiPacker& packer) const;
  void Pack(SendError&& error, ApiPacker& packer) const;
};
}  // namespace ae

#endif  // AETHER_API_PROTOCOL_API_PROTOCOL_H_ */
