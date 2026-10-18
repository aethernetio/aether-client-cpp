/*
 * Copyright 2026 Aethernet Inc.
 */
#include "stream_matrix/bench_streaming_receiver.h"

#include <algorithm>
#include <cstring>

namespace ae::stream_matrix_bench {

bool IsThptWireData(DataBuffer const& data) {
  if (data.size() < sizeof(ThptWireHeader)) {
    return false;
  }
  std::uint32_t magic{};
  std::memcpy(&magic, data.data(), sizeof(magic));
  return magic == kThptMagic;
}

StreamingReceiverState::StreamingReceiverState(std::vector<std::uint8_t> const& blob,
                                               std::uint64_t run_id)
    : blob_{blob}, run_id_{run_id} {
  received_.assign(kThroughputTotalBytes, false);
}

void StreamingReceiverState::Reset(std::uint64_t run_id) {
  run_id_ = run_id;
  received_.assign(kThroughputTotalBytes, false);
  seen_chunks_.clear();
  chunk_recv_.clear();
  bytes_unique_ = 0;
  dup_chunks_ = 0;
  integrity_fail_ = false;
  first_rx_ = {};
  last_rx_ = {};
}

void StreamingReceiverState::Feed(DataBuffer const& data, BenchInstant now) {
  std::size_t off = 0;
  while (off + sizeof(ThptWireHeader) <= data.size()) {
    ThptWireHeader hdr{};
    std::memcpy(&hdr, data.data() + off, sizeof(hdr));
    if (hdr.magic != kThptMagic) {
      break;
    }
    std::size_t const frame_len = sizeof(ThptWireHeader) + hdr.payload_len;
    if (off + frame_len > data.size()) {
      break;
    }
    std::vector<std::uint8_t> frame(data.data() + off,
                                    data.data() + off + frame_len);
    AcceptFrame(DataBuffer{std::move(frame)}, now);
    off += frame_len;
  }
}

void StreamingReceiverState::AcceptFrame(DataBuffer const& data, BenchInstant now) {
  auto hdr = ParseThptHeader(data);
  if (!hdr || hdr->run_id != run_id_) {
    return;
  }
  if (!VerifyThptChunk(data, blob_, run_id_)) {
    integrity_fail_ = true;
    return;
  }
  if (seen_chunks_.count(hdr->chunk_index)) {
    ++dup_chunks_;
    return;
  }
  seen_chunks_.insert(hdr->chunk_index);
  chunk_recv_[hdr->chunk_index] = now;
  for (std::uint32_t i = 0; i < hdr->payload_len; ++i) {
    received_[hdr->byte_offset + i] = true;
  }
  if (!first_rx_.time_since_epoch().count()) {
    first_rx_ = now;
  }
  last_rx_ = now;
  bytes_unique_ += hdr->payload_len;
}

bool StreamingReceiverState::Complete() const {
  return bytes_unique_ == kThroughputTotalBytes &&
         std::all_of(received_.begin(), received_.end(), [](bool b) { return b; });
}

std::size_t StreamingReceiverState::bytes_unique() const { return bytes_unique_; }
std::uint64_t StreamingReceiverState::dup_chunks() const { return dup_chunks_; }
bool StreamingReceiverState::integrity_fail() const { return integrity_fail_; }
BenchInstant StreamingReceiverState::first_rx() const { return first_rx_; }
BenchInstant StreamingReceiverState::last_rx() const { return last_rx_; }

BenchInstant StreamingReceiverState::recv_time_for_chunk(
    std::uint64_t chunk_index) const {
  auto it = chunk_recv_.find(chunk_index);
  if (it == chunk_recv_.end()) {
    return {};
  }
  return it->second;
}

}  // namespace ae::stream_matrix_bench
