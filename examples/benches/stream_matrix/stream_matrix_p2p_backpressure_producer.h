/*
 * Continuous producer for P2pSafeStream::TryWrite backpressure.
 */
#ifndef EXAMPLES_BENCHES_STREAM_MATRIX_STREAM_MATRIX_P2P_BACKPRESSURE_PRODUCER_H_
#define EXAMPLES_BENCHES_STREAM_MATRIX_STREAM_MATRIX_P2P_BACKPRESSURE_PRODUCER_H_

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "aether/client_messages/p2p_safe_message_stream.h"
#include "aether/events/events.h"
#include "aether/tasks/details/task_subsctiption.h"
#include "aether/write_action/write_action.h"

namespace ae::stream_matrix_bench {

struct P2pBackpressureProducerStats {
  std::uint64_t would_block_count{0};
  std::uint64_t resume_count{0};
  std::uint64_t accepted_writes{0};
  std::uint64_t success_terminals{0};
  std::uint64_t fail_terminals{0};
  std::uint64_t stop_terminals{0};
  std::uint64_t error_count{0};
  std::size_t peak_send_buffer_used{0};
  std::size_t peak_send_buffer_in_terminal{0};
  std::size_t residual_send_buffer_used{0};
  std::size_t residual_send_buffer_after_drain{0};
  std::size_t peak_in_flight_writes{0};
};

class P2pBackpressureProducer {
 public:
  using DoneFn = std::function<void(bool success)>;
  using InFlightBudgetFn = std::function<bool(std::size_t extra_in_flight)>;

  P2pBackpressureProducer(AeContext const& ctx, P2pSafeStream& stream,
                          std::vector<std::uint8_t> payload);

  void Start(std::size_t chunk_bytes, DoneFn on_done,
             InFlightBudgetFn in_flight_budget = {});

  P2pBackpressureProducerStats const& stats() const { return stats_; }
  std::size_t offset() const { return offset_; }
  bool finished() const { return finished_; }
  bool hard_failed() const { return hard_failed_; }
  std::uint64_t done_calls() const { return done_calls_; }

 private:
  struct InFlightEntry {
    std::uint64_t write_id{};
    Subscription status_sub{};
  };

  void SchedulePump();
  void Pump();
  void OnWriteTerminal(std::uint64_t write_id, WriteAction::Status st);
  void FailHard();
  void MaybeFinish();
  void CompleteFinished();
  void TrackPeaks();
  void TrackPeaksInTerminal();

  AeContext ctx_;
  P2pSafeStream& stream_;
  std::vector<std::uint8_t> payload_;
  std::size_t offset_{0};
  std::size_t chunk_bytes_{0};
  bool finished_{false};
  bool waiting_capacity_{false};
  bool hard_failed_{false};
  bool terminal_ok_{true};
  std::size_t in_flight_{0};
  std::uint64_t next_write_id_{1};
  P2pBackpressureProducerStats stats_;
  Subscription capacity_sub_;
  Subscription stream_update_sub_;
  std::deque<InFlightEntry> in_flight_entries_;
  TaskSubscription pump_sub_;
  InFlightBudgetFn in_flight_budget_;
  DoneFn on_done_;
  std::uint64_t done_calls_{0};
};

}  // namespace ae::stream_matrix_bench

#endif
