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

void P2pBackpressureProducer::Start(std::size_t chunk_bytes, DoneFn on_done) {
  chunk_bytes_ = chunk_bytes;
  on_done_ = std::move(on_done);
  finished_ = false;
  offset_ = 0;
  terminal_ok_ = true;
  in_flight_ = 0;
  waiting_capacity_ = false;
  stats_ = {};
  capacity_sub_.Reset();
  write_status_subs_.clear();
  capacity_sub_ = stream_.write_capacity_event().Subscribe([this]() {
    if (waiting_capacity_) {
      ++stats_.resume_count;
      waiting_capacity_ = false;
      SchedulePump();
    }
  });
  SchedulePump();
}

void P2pBackpressureProducer::SchedulePump() {
  ctx_.scheduler().Task([this]() { Pump(); });
}

void P2pBackpressureProducer::TrackPeaks() {
  stats_.peak_send_buffer_used =
      std::max(stats_.peak_send_buffer_used, stream_.send_buffer_used_bytes());
  stats_.peak_in_flight_writes = std::max(stats_.peak_in_flight_writes, in_flight_);
}

void P2pBackpressureProducer::MaybeFinish() {
  if (finished_) {
    return;
  }
  if (offset_ >= payload_.size() && in_flight_ == 0) {
    finished_ = true;
    stats_.residual_send_buffer_used = stream_.send_buffer_used_bytes();
    capacity_sub_.Reset();
    write_status_subs_.clear();
    if (on_done_) {
      on_done_(terminal_ok_);
    }
  }
}

void P2pBackpressureProducer::OnWriteTerminal(WriteAction::Status st) {
  if (in_flight_ > 0) {
    --in_flight_;
  }
  if (!write_status_subs_.empty()) {
    write_status_subs_.front().Reset();
    write_status_subs_.erase(write_status_subs_.begin());
  }
  if (st != WriteAction::Status::kSuccess) {
    terminal_ok_ = false;
  }
  TrackPeaks();
  MaybeFinish();
  if (!finished_ && !waiting_capacity_) {
    SchedulePump();
  }
}

void P2pBackpressureProducer::Pump() {
  if (finished_) {
    return;
  }
  TrackPeaks();
  if (offset_ < payload_.size() && !waiting_capacity_) {
    auto const remaining = payload_.size() - offset_;
    auto const take = std::min(chunk_bytes_, remaining);
    DataBuffer chunk(payload_.begin() + static_cast<std::ptrdiff_t>(offset_),
                     payload_.begin() + static_cast<std::ptrdiff_t>(offset_ + take));
    auto offer = stream_.TryWrite(chunk);
    if (offer.result == P2pWriteOfferResult::kWouldBlock) {
      ++stats_.would_block_count;
      waiting_capacity_ = true;
    } else if (offer.result == P2pWriteOfferResult::kError) {
      ++stats_.error_count;
      terminal_ok_ = false;
      finished_ = true;
      stats_.residual_send_buffer_used = stream_.send_buffer_used_bytes();
      capacity_sub_.Reset();
      write_status_subs_.clear();
      if (on_done_) {
        on_done_(false);
      }
      return;
    } else {
      ++stats_.accepted_writes;
      offset_ += take;
      ++in_flight_;
      TrackPeaks();
      write_status_subs_.push_back(offer.action->status_event().Subscribe(
          [this](WriteAction::Status st) { OnWriteTerminal(st); }));
    }
  }
  MaybeFinish();
}

}  // namespace ae::stream_matrix_bench
