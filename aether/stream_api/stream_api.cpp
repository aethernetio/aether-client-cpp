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

#include "aether/stream_api/stream_api.h"

#include <cstddef>
#include <utility>

#include "aether/api_protocol/api_message.h"
#include "aether/api_protocol/api_protocol.h"

namespace ae {
bool StreamApi::LoadResult(MessageId message_id, ApiParser& parser) {
  switch (message_id) {
    case Stream::kMessageCode:
      parser.Load<Stream>(*this);
      return true;
    default:
      return false;
  }
}

void StreamApi::LoadFactory(MessageId message_id, ApiParser& parser) {
  [[maybe_unused]] auto res = LoadResult(message_id, parser);
  assert(res);
}

void StreamApi::Execute(Stream&& message, ApiParser& parser) {
  parser.Context().MessageNotify(std::move(message));
}

void StreamApi::Pack(Stream&& message, ApiPacker& packer) {
  packer.Pack(Stream::kMessageCode, std::move(message));
}

StreamApiImpl::StreamApiImpl(ProtocolContext& protocol_context)
    : stream{protocol_context} {}

void StreamApiImpl::Stream(ApiParser& parser, StreamId stream_id,
                           DataBuffer data) {
  // TODO: impl without MessageNotify
  parser.Context().MessageNotify(
      StreamApi::Stream{{}, stream_id, std::move(data)});
}

std::uint8_t StreamIdGenerator::GetNextClientStreamId() {
  static StreamId stream_id = 1;
  auto val = stream_id;
  stream_id += 2;
  return val;
}

std::uint8_t StreamIdGenerator::GetNextServerStreamId() {
  static StreamId stream_id = 2;
  auto val = stream_id;
  stream_id += 2;
  return val;
}

static constexpr std::size_t kStreamMessageOverhead =
    1 + 1 +
    sizeof(
        PackedSize::ValueType);  // message code + stream id +  child data size

StreamApiGate::StreamApiGate(ProtocolContext& protocol_context,
                             StreamId stream_id)
    : protocol_context_{&protocol_context}, stream_id_{stream_id} {
  read_subscription_ =
      protocol_context_->MessageEvent<StreamApi::Stream>().Subscribe(
          *this, MethodPtr<&StreamApiGate::OnStream>{});
}

StreamApiGate::StreamApiGate(StreamApiGate&& other) noexcept
    : protocol_context_{other.protocol_context_}, stream_id_{other.stream_id_} {
  read_subscription_ =
      protocol_context_->MessageEvent<StreamApi::Stream>().Subscribe(
          *this, MethodPtr<&StreamApiGate::OnStream>{});
}

StreamApiGate& StreamApiGate::operator=(StreamApiGate&& other) noexcept {
  if (this != &other) {
    protocol_context_ = other.protocol_context_;
    stream_id_ = other.stream_id_;
    read_subscription_ =
        protocol_context_->MessageEvent<StreamApi::Stream>().Subscribe(
            *this, MethodPtr<&StreamApiGate::OnStream>{});
  }
  return *this;
}

DataBuffer StreamApiGate::WriteIn(DataBuffer&& buffer) {
  return PacketBuilder{*protocol_context_,
                       PackMessage{
                           StreamApi{},
                           StreamApi::Stream{{}, stream_id_, std::move(buffer)},
                       }};
}

void StreamApiGate::WriteOut(DataBuffer const& buffer) {
  out_data_event_.Emit(buffer);
}

EventSubscriber<void(DataBuffer const& data)> StreamApiGate::out_data_event() {
  return EventSubscriber{out_data_event_};
}

std::size_t StreamApiGate::Overhead() const { return kStreamMessageOverhead; }

void StreamApiGate::OnStream(MessageEventData<StreamApi::Stream> const& msg) {
  auto const& message = msg.message();
  if (stream_id_ == message.stream_id) {
    out_data_event_.Emit(message.child_data.PackData());
  }
}

}  // namespace ae
