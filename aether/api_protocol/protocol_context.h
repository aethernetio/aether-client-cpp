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

#ifndef AETHER_API_PROTOCOL_PROTOCOL_CONTEXT_H_
#define AETHER_API_PROTOCOL_PROTOCOL_CONTEXT_H_

#include <functional>
#include <map>
#include <stack>
#include <memory>
#include <cstdint>
#include <utility>

#include "aether/memory.h"
#include "aether/events/events.h"
#include "aether/actions/action.h"
#include "aether/api_protocol/request_id.h"

namespace ae {
class ApiParser;

template <typename TMessage>
class MessageEventData {
  using MessageType = TMessage;

 public:
  MessageEventData(MessageType&& message,
                   std::map<std::uint32_t, void const*> const* api_class_map,
                   void* user_data)
      : message_{std::move(message)},
        api_class_map_{api_class_map},
        user_data_{user_data} {}

  template <typename TApiClass>
  TApiClass const* GetApiClass() const {
    if (!api_class_map_) {
      return nullptr;
    }
    auto it = api_class_map_->find(TApiClass::kClassId);
    if (it == api_class_map_->end()) {
      return nullptr;
    }
    return static_cast<TApiClass const*>(it->second);
  }

  void* UserData() const { return user_data_; }

  MessageType const& message() const { return message_; }

 private:
  MessageType message_;
  std::map<std::uint32_t, void const*> const* api_class_map_;
  void* user_data_;
};

class IMessageEvent {
 public:
  virtual ~IMessageEvent() = default;
};

template <typename TMessage>
class MessageEvent : public IMessageEvent {
 public:
  auto Subscriber() { return EventSubscriber{event_}; }
  void Emit(MessageEventData<TMessage> const& message) { event_.Emit(message); }

 private:
  Event<void(MessageEventData<TMessage> const&)> event_;
};

class ProtocolContext {
 public:
  ProtocolContext();
  ~ProtocolContext();

  template <typename TMessage,
            auto MessageTypeId = std::decay_t<TMessage>::kMessageId>
  [[nodiscard]] auto MessageEvent() {
    auto it = messages_events_.find(MessageTypeId);
    if (it == messages_events_.end()) {
      auto [added_it, _] = messages_events_.emplace(
          MessageTypeId, std::make_unique<ae::MessageEvent<TMessage>>());
      it = added_it;
    }
    auto& message_event =
        *static_cast<ae::MessageEvent<TMessage>*>(it->second.get());
    return message_event.Subscriber();
  }

  template <typename TMessage,
            auto MessageTypeId = std::decay_t<TMessage>::kMessageId>
  void MessageNotify(TMessage&& message) {
    using MessageType = std::decay_t<TMessage>;
    using MessageEventType = MessageEventData<std::decay_t<MessageType>>;

    auto event_it = messages_events_.find(MessageTypeId);
    if (event_it == messages_events_.end()) {
      return;
    }
    auto& message_event =
        *static_cast<ae::MessageEvent<MessageType>*>(event_it->second.get());

    message_event.Emit(MessageEventType{std::forward<TMessage>(message),
                                        &api_class_map_, TopUserData()});
  }

  void PushApiClass(std::uint32_t class_id, void const* api_class);
  void PopApiClass(std::uint32_t class_id);

  void PushUserData(void* data);
  void PopUserData();
  void* TopUserData();

  void AddSendResultCallback(RequestId request_id,
                             std::function<void(ApiParser& parser)> callback);
  void AddSendErrorCallback(
      RequestId request_id,
      std::function<void(struct SendError const& error)> callback);

  void SetSendResultResponse(RequestId request_id, ApiParser& parser);
  void SetSendErrorResponse(struct SendError const& error, ApiParser& parser);

  void PushPacketStack(class PacketStack& packet_stack);
  void PopPacketStack();
  class PacketStack* packet_stack();

 private:
  // map global message id to events
  std::map<std::uint32_t, std::unique_ptr<IMessageEvent>> messages_events_;

  std::stack<void*> user_data_stack_;
  std::map<std::uint32_t, void const*> api_class_map_;

  std::map<RequestId, std::function<void(ApiParser& parser)>>
      send_result_events_;
  std::map<RequestId, std::function<void(struct SendError const& error)>>
      send_error_events_;

  std::stack<class PacketStack*> packet_stacks_;
};
}  // namespace ae

#endif  // AETHER_API_PROTOCOL_PROTOCOL_CONTEXT_H_
