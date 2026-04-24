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

#ifndef TESTS_TEST_STREAM_MOCK_BAD_STREAMS_H_
#define TESTS_TEST_STREAM_MOCK_BAD_STREAMS_H_

#include <queue>
#include <optional>

#include "aether/ae_context.h"
#include "aether/stream_api/istream.h"

namespace ae {
namespace bad_streams_internal {
class DoneStreamWriteAction : public WriteAction {
 public:
  explicit DoneStreamWriteAction(AeContext const& ae_context);
  using WriteAction::WriteAction;
};
}  // namespace bad_streams_internal

class LostPacketsStream : public ByteStream {
 public:
  LostPacketsStream(AeContext const& ae_context, float loss_rate);

  WriteAction& Write(DataBuffer&& data_buffer) override;

  void LinkOut(ByteIStream& out) override;

 private:
  AeContext ae_context_;
  float loss_rate_;
  std::optional<bad_streams_internal::DoneStreamWriteAction> dsw_;
};

class PacketDelayStream : public ByteStream {
 public:
  PacketDelayStream(AeContext const& ae_context, float delay_rate,
                    Duration max_delay);

  WriteAction& Write(DataBuffer&& data_buffer) override;

  void LinkOut(ByteIStream& out) override;

 private:
  AeContext ae_context_;
  float delay_rate_;
  Duration max_delay_;
  std::queue<DataBuffer> data_queue_;
  std::optional<bad_streams_internal::DoneStreamWriteAction> dsw_;
};
}  // namespace ae

#endif  // TESTS_TEST_STREAM_MOCK_BAD_STREAMS_H_
