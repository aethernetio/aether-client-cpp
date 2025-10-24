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

#ifndef AETHER_STREAM_API_STREAM_API_H_
#define AETHER_STREAM_API_STREAM_API_H_

#include "aether/events/events.h"
#include "aether/types/data_buffer.h"
#include "aether/api_protocol/api_method.h"
#include "aether/events/event_subscription.h"
#include "aether/api_protocol/api_protocol.h"
#include "aether/api_protocol/api_class_impl.h"

namespace ae {
using StreamId = std::uint8_t;

class StreamApiImpl : public ApiClass {
 public:
  using StreamEvent = Event<void(StreamId, DataBuffer const& data)>;

  explicit StreamApiImpl(ProtocolContext& protocol_context);
  virtual ~StreamApiImpl() = default;

  void Stream(ApiParser& parser, StreamId stream_id, DataBuffer data);
  using ApiMethods = ImplList<RegMethod<02, &StreamApiImpl::Stream>>;

  Method<02, void(StreamId stream_id, DataBuffer data)> stream;

  StreamEvent::Subscriber stream_event();

 private:
  StreamEvent stream_event_;
};

class StreamIdGenerator {
 public:
  static StreamId GetNextClientStreamId();
  static StreamId GetNextServerStreamId();
};

class StreamApiGate {
 public:
  StreamApiGate(StreamApiImpl& stream_api, StreamId stream_id);

  DataBuffer WriteIn(DataBuffer&& buffer);
  void WriteOut(DataBuffer const& buffer);
  std::size_t Overhead() const;

  EventSubscriber<void(DataBuffer const& data)> out_data_event();

 private:
  void OnStream(StreamId stream_id, DataBuffer const& data);

  StreamId stream_id_;
  StreamApiImpl* stream_api_;
  Subscription read_subscription_;
  Event<void(DataBuffer const& data)> out_data_event_;
};

}  // namespace ae

#endif  // AETHER_STREAM_API_STREAM_API_H_
