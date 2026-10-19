/*
 * Continuous producer for P2pSafeStream::TryWrite backpressure.
 */
#include "stream_matrix/stream_matrix_p2p_backpressure_producer.h"

#include <algorithm>
#include <utility>

namespace ae::stream_matrix_bench {

P2pBackpressureProducer::P2pBackpressureProducer(AeContext const& ctx,
                                                 P2pSafeStream& stream,
                                                 std::vector<std::uint8_t> payload)
    : ctx_{ctx}, stream_{stream}, payload_{std::move(payload)} {}

void P2pBackpressureProducer::Start(std::size_t chunk_bytes, DoneFn on_done,
                                     InFlightBudgetFn in_flight_budget) {
  chunk_bytes_ = chunk_bytes;
  on_done_ = std::move(on_done);
  in_flight_budget_ = std::move(in_flight_budget);
  finished_ = false;
  hard_failed_ = false;
  offset_ = 0;
  terminal_ok_ = true;
  in_flight_ = 0;
  next_write_id_ = 1;
  waiting_capacity_ = false;
  stats_ = {};
  capacity_sub_.Reset();
  stream_update_sub_.Reset();
  pump_sub_.Reset();
  in_flight_entries_.clear();

  capacity_sub_ = stream_.write_capacity_event().Subscribe([this]() {
    if (waiting_capacity_) {
      ++stats_.resume_count;
    }
    waiting_capacity_ = false;
    SchedulePump();
  });
  stream_update_sub_ = stream_.stream_update_event().Subscribe([this]() {
    auto const info = stream_.stream_info();
    if (info.link_state == LinkState::kLinkError) {
      FailHard();
      return;
    }
    if (info.link_state == LinkState::kLinked) {
      if (waiting_capacity_) {
        ++stats_.resume_count;
      }
      waiting_capacity_ = false;
      SchedulePump();
    }
  });
  SchedulePump();
}

void P2pBackpressureProducer::SchedulePump() {
  if (finished_ || hard_failed_ || pump_sub_) {
    return;
  }
  pump_sub_ = ctx_.scheduler().Task([this]() {
    pump_sub_.Reset();
    Pump();
  });
}

void P2pBackpressureProducer::TrackPeaks() {
  stats_.peak_send_buffer_used =
      std::max(stats_.peak_send_buffer_used, stream_.send_buffer_used_bytes());
  stats_.peak_in_flight_writes = std::max(stats_.peak_in_flight_writes, in_flight_);
}

void P2pBackpressureProducer::TrackPeaksInTerminal() {
  stats_.peak_send_buffer_in_terminal =
      std::max(stats_.peak_send_buffer_in_terminal, stream_.send_buffer_used_bytes());
}

void P2pBackpressureProducer::DrainResidual() {
  stats_.residual_send_buffer_used = stream_.send_buffer_used_bytes();
  ctx_.scheduler().Task([this]() {
    stats_.residual_send_buffer_after_drain = stream_.send_buffer_used_bytes();
  });
}

void P2pBackpressureProducer::FailHard() {
  if (finished_) {
    return;
  }
  hard_failed_ = true;
  terminal_ok_ = false;
  ++stats_.error_count;
  pump_sub_.Reset();
  waiting_capacity_ = false;
  MaybeFinish();
}

void P2pBackpressureProducer::MaybeFinish() {
  if (finished_) {
    return;
  }
  if (hard_failed_) {
    return;
  }
  bool const payload_done = offset_ >= payload_.size();
  if (in_flight_ != 0) {
    return;
  }
  if (!hard_failed_ && !payload_done) {
    return;
  }
  finished_ = true;
  capacity_sub_.Reset();
  stream_update_sub_.Reset();
  pump_sub_.Reset();
  for (auto& e : in_flight_entries_) {
    e.status_sub.Reset();
  }
  in_flight_entries_.clear();
  DrainResidual();
  if (on_done_) {
    on_done_(terminal_ok_);
  }
}

void P2pBackpressureProducer::OnWriteTerminal(std::uint64_t write_id,
                                              WriteAction::Status st) {
  TrackPeaksInTerminal();
  for (auto it = in_flight_entries_.begin(); it != in_flight_entries_.end(); ++it) {
    if (it->write_id != write_id) {
      continue;
    }
    it->status_sub.Reset();
    in_flight_entries_.erase(it);
    break;
  }
  if (in_flight_ > 0) {
    --in_flight_;
  }
  switch (st) {
    case WriteAction::Status::kSuccess:
      ++stats_.success_terminals;
      break;
    case WriteAction::Status::kFail:
      ++stats_.fail_terminals;
      terminal_ok_ = false;
      hard_failed_ = true;
      break;
    case WriteAction::Status::kStop:
      ++stats_.stop_terminals;
      terminal_ok_ = false;
      hard_failed_ = true;
      break;
  }
  TrackPeaks();
  if (hard_failed_) {
    pump_sub_.Reset();
    waiting_capacity_ = false;
  }
  MaybeFinish();
  if (!finished_ && !waiting_capacity_) {
    SchedulePump();
  }
}

void P2pBackpressureProducer::Pump() {
  if (finished_ || hard_failed_) {
    return;
  }
  TrackPeaks();
  while (offset_ < payload_.size() && !waiting_capacity_) {
    if (in_flight_budget_ && !in_flight_budget_(in_flight_ + 1)) {
      waiting_capacity_ = true;
      break;
    }
    auto const remaining = payload_.size() - offset_;
    auto const take = std::min(chunk_bytes_, remaining);
    DataBuffer chunk(payload_.begin() + static_cast<std::ptrdiff_t>(offset_),
                     payload_.begin() + static_cast<std::ptrdiff_t>(offset_ + take));
    auto offer = stream_.TryWrite(chunk);
    if (offer.result == P2pWriteOfferResult::kWouldBlock) {
      ++stats_.would_block_count;
      waiting_capacity_ = true;
      break;
    }
    if (offer.result == P2pWriteOfferResult::kError) {
      FailHard();
      return;
    }
    ++stats_.accepted_writes;
    offset_ += take;
    ++in_flight_;
    auto const write_id = next_write_id_++;
    TrackPeaks();
    InFlightEntry entry{.write_id = write_id};
    entry.status_sub = offer.action->status_event().Subscribe(
        [this, write_id](WriteAction::Status st) { OnWriteTerminal(write_id, st); });
    in_flight_entries_.push_back(std::move(entry));
  }
  MaybeFinish();
}

}  // namespace ae::stream_matrix_bench
