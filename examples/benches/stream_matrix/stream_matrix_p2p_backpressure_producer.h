/*
 * Continuous producer for P2pSafeStream::TryWrite backpressure.
 */
#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_STREAM_MATRIX_P2P_BACKPRESSURE_PRODUCER_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_STREAM_MATRIX_P2P_BACKPRESSURE_PRODUCER_H_

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "aether/client_messages/p2p_safe_message_stream.h"
#include "aether/events/events.h"
#include "aether/write_action/write_action.h"

namespace ae::stream_matrix_bench {

struct P2pBackpressureProducerStats {
  std::uint64_t would_block_count{0};
  std::uint64_t resume_count{0};
  std::uint64_t accepted_writes{0};
  std::uint64_t error_count{0};
  std::size_t peak_send_buffer_used{0};
  std::size_t residual_send_buffer_used{0};
  std::size_t peak_in_flight_writes{0};
};

class P2pBackpressureProducer {
 public:
  P2pBackpressureProducer(AeContext const& ctx, P2pSafeStream& stream,
                          std::vector<std::uint8_t> payload);

  using DoneFn = std::function<void(bool success)>;

  void Start(std::size_t chunk_bytes, DoneFn on_done);
  void Pump();

  P2pBackpressureProducerStats const& stats() const { return stats_; }
  std::size_t offset() const { return offset_; }
  bool finished() const { return finished_; }

 private:
  void SchedulePump();
  void OnWriteTerminal(WriteAction::Status st);
  void MaybeFinish();
  void TrackPeaks();

  AeContext ctx_;
  P2pSafeStream& stream_;
  std::vector<std::uint8_t> payload_;
  std::size_t offset_{0};
  std::size_t chunk_bytes_{0};
  bool finished_{false};
  bool waiting_capacity_{false};
  bool terminal_ok_{true};
  std::size_t in_flight_{0};
  P2pBackpressureProducerStats stats_;
  Subscription capacity_sub_;
  std::vector<Subscription> write_status_subs_;
  DoneFn on_done_;
};

}  // namespace ae::stream_matrix_bench

#endif
