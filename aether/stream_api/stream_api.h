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

#include "aether/crc.h"

#include "aether/api_protocol/child_data.h"
#include "aether/api_protocol/api_protocol.h"

#include "aether/stream_api/istream.h"

namespace ae {
using StreamId = std::uint8_t;

class StreamApi : public ApiClass {
 public:
  static constexpr auto kClassId = crc32::checksum_from_literal("StreamApi");

  struct Stream : public Message<Stream> {
    static constexpr auto kMessageId =
        crc32::checksum_from_literal("StreamApi::Stream");
    static constexpr MessageId kMessageCode = 2;

    template <typename T>
    void Serializator(T& s) {
      s & stream_id & child_data;
    }

    StreamId stream_id;
    ChildData child_data;
  };

  bool LoadResult(MessageId message_id, ApiParser& parser);
  void LoadFactory(MessageId message_id, ApiParser& parser) override;

  void Execute(Stream&& message, ApiParser& parser);
  void Pack(Stream&& message, ApiPacker& packer);
};

class StreamIdGenerator {
 public:
  static StreamId GetNextClientStreamId();
  static StreamId GetNextServerStreamId();
};

class StreamApiGate final : public ByteGate {
 public:
  StreamApiGate(ProtocolContext& protocol_context, StreamId stream_id);
  StreamApiGate(StreamApiGate&& other) noexcept;

  StreamApiGate& operator=(StreamApiGate const& other) = delete;
  StreamApiGate& operator=(StreamApiGate&& other) noexcept;

  ActionView<StreamWriteAction> Write(DataBuffer&& buffer,
                                      TimePoint current_time) override;

  void LinkOut(OutGate& out) override;

  StreamInfo stream_info() const override;

  void PutData(DataBuffer const& data);

 private:
  void OnStream(MessageEventData<StreamApi::Stream> const& msg);

  std::reference_wrapper<ProtocolContext> protocol_context_;
  StreamId stream_id_;
  StreamApi api_;
  Subscription read_subscription_;
};

}  // namespace ae

#endif  // AETHER_STREAM_API_STREAM_API_H_
