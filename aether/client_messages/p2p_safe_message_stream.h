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

#ifndef AETHER_CLIENT_MESSAGES_P2P_SAFE_MESSAGE_STREAM_H_
#define AETHER_CLIENT_MESSAGES_P2P_SAFE_MESSAGE_STREAM_H_

#include "aether/ae_context.h"
#include "aether/common.h"
#include "aether/config.h"
#include "aether/memory.h"
#include "aether/write_action/failed_write_action.h"

#include <optional>

#include "aether/safe_stream/safe_stream_config.h"
#include "aether/stream_api/istream.h"
#include "aether/stream_api/sized_packet_gate.h"

namespace ae {
template <std::size_t Capacity>
class SafeStream;

static constexpr inline std::size_t kP2pSafeStreamCapacity =
    AE_SAFE_STREAM_CAPACITY;

enum class P2pWriteOfferResult {
  kAccepted,
  kWouldBlock,
  kError,
};

struct P2pWriteOffer {
  P2pWriteOfferResult result{P2pWriteOfferResult::kError};
  WriteAction* action{nullptr};
};

class P2pSafeStream final : public ByteIStream {
 public:
  using SafeStreamImpl = SafeStream<kP2pSafeStreamCapacity>;

  P2pSafeStream(AeContext const& ae_context, SafeStreamConfig const& config,
                std::shared_ptr<ByteIStream> p2p_stream);
  ~P2pSafeStream() override;

  AE_CLASS_NO_COPY_MOVE(P2pSafeStream)

  WriteAction& Write(DataBuffer&& data) override;
  /** Non-blocking offer: on WOULD_BLOCK, data is unchanged. */
  P2pWriteOffer TryWrite(DataBuffer const& data);
  bool CanWritePayload(std::size_t payload_bytes) const;
  std::size_t send_buffer_free_bytes() const;
  std::size_t send_buffer_used_bytes() const;
  Event<void()> const& write_capacity_event() const;
  StreamInfo stream_info() const override;
  StreamUpdateEvent const& stream_update_event() override;
  OutDataEvent const& out_data_event() override;
  void Restream() override;

 private:
  AeContext ae_context_;
  SizedPacketGate sized_packet_gate_;
  std::unique_ptr<SafeStreamImpl> safe_stream_;
  std::shared_ptr<ByteIStream> p2p_stream_;
  OutDataEvent out_data_event_;
  std::array<Subscription, 2> out_data_sub_;
  std::optional<FailedWriteAction> failed_write_;
};

}  // namespace ae

#endif  // AETHER_CLIENT_MESSAGES_P2P_SAFE_MESSAGE_STREAM_H_
