/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef AETHER_ADAPTIVE_DATAGRAM_ADAPTIVE_DATAGRAM_STREAM_H_
#define AETHER_ADAPTIVE_DATAGRAM_ADAPTIVE_DATAGRAM_STREAM_H_

#include <functional>
#include <memory>

#include "aether/adaptive_datagram/adaptive_datagram_config.h"
#include "aether/adaptive_datagram/rate_controller.h"
#include "aether/ae_context.h"
#include "aether/stream_api/istream.h"

namespace ae {
namespace adaptive_datagram {
enum class DeliveryStatus {
  kDelivered,
  kLost,
  kUnconfirmed,
  kLate,
  kExpired,
  kCancelled,
  kLocalFailure
};
struct DeliveryResult {
  std::uint64_t sequence;
  DeliveryStatus status;
  std::size_t payload_bytes;
  Duration latency;
};
struct StreamStatistics {
  std::uint64_t accepted{};
  std::uint64_t sent{};
  std::uint64_t delivered{};
  std::uint64_t lost{};
  std::uint64_t unconfirmed{};
  std::uint64_t late{};
  std::uint64_t expired{};
  std::uint64_t cancelled{};
  std::uint64_t local_failures{};
  std::uint64_t rejected{};
  std::uint64_t duplicates{};
  std::uint64_t malformed{};
  std::uint64_t feedback_sent{};
  std::uint64_t wire_bytes{};
  std::size_t queued{};
  std::size_t in_flight{};
};
}  // namespace adaptive_datagram

// One independent, unordered message per datagram. Link to a dedicated
// P2pStream (or any datagram ByteIStream); both endpoints must use this
// protocol. The context, clock captures and linked stream must outlive this
// object. WriteAction reports local handoff only; delivery_event reports peer
// outcomes. All methods and the lower stream's events run on one application
// thread.
class AdaptiveDatagramStream final : public ByteIStream {
 public:
  using Clock = std::function<TimePoint()>;
  using DeliveryEvent = Event<void(adaptive_datagram::DeliveryResult const&)>;

  AdaptiveDatagramStream(AeContext const& context,
                         adaptive_datagram::Config config, Clock clock = {});
  ~AdaptiveDatagramStream() override;
  AE_CLASS_NO_COPY_MOVE(AdaptiveDatagramStream);

  void LinkOut(ByteIStream& stream);
  void Unlink();
  WriteAction& Write(DataBuffer&& data) override;
  StreamInfo stream_info() const override;
  StreamUpdateEvent const& stream_update_event() override;
  OutDataEvent const& out_data_event() override;
  void Restream() override;
  DeliveryEvent const& delivery_event();
  adaptive_datagram::StreamStatistics statistics() const;
  adaptive_datagram::RateControllerMetrics const& metrics() const;
  std::uint64_t rate_bytes_per_second() const;
  bool valid() const;

 private:
  struct Impl;
  std::shared_ptr<Impl> impl_;
};
}  // namespace ae
#endif  // AETHER_ADAPTIVE_DATAGRAM_ADAPTIVE_DATAGRAM_STREAM_H_
