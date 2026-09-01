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

#ifndef EXAMPLES_PROBE_RECEIVER_PROBE_PROTOCOL_H_
#define EXAMPLES_PROBE_RECEIVER_PROBE_PROTOCOL_H_

#include <cstddef>
#include <cstdint>

// Application-level message format exchanged over an ordinary Aether P2P
// stream between the adaptive Wi-Fi probe firmware and the desktop
// probe_receiver. This is not part of the Aether protocol: the wire format of
// Aether itself is unchanged, these bytes are opaque user payload.
//
// All integers are packed little-endian with no padding so the same layout is
// produced by the ESP32 firmware and parsed by the desktop receiver.

namespace ae::probe {

enum class ProbeMsgType : std::uint8_t {
  kProbeData = 0xB0,
  kProbeQuery = 0xB1,
  kProbeResult = 0xB2,
  kHotData = 0xB3,
  kHotSummary = 0xB4,
  kBenchArm = 0xB5,
  kBenchData = 0xB6,
  kBenchSummary = 0xB7,
};

// Longest encoded message (kHotData) plus headroom.
static constexpr std::size_t kMaxProbeMessageSize = 96;

// Sequential little-endian byte writer over caller-owned storage.
class ProbeWriter {
 public:
  ProbeWriter(std::uint8_t* data, std::size_t capacity)
      : data_{data}, capacity_{capacity} {}

  bool U8(std::uint8_t value) {
    if (!Fits(1)) {
      return false;
    }
    data_[size_++] = value;
    return true;
  }

  bool U16(std::uint16_t value) {
    if (!Fits(2)) {
      return false;
    }
    data_[size_++] = static_cast<std::uint8_t>(value & 0xFFu);
    data_[size_++] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    return true;
  }

  bool U32(std::uint32_t value) {
    if (!Fits(4)) {
      return false;
    }
    for (int shift = 0; shift < 32; shift += 8) {
      data_[size_++] = static_cast<std::uint8_t>((value >> shift) & 0xFFu);
    }
    return true;
  }

  // Two's complement, so the reader gets the sign back on any conforming
  // platform without depending on a signed shift.
  bool I32(std::int32_t value) {
    return U32(static_cast<std::uint32_t>(value));
  }

  std::size_t size() const { return size_; }
  bool ok() const { return ok_; }

 private:
  bool Fits(std::size_t need) {
    if (size_ + need > capacity_) {
      ok_ = false;
      return false;
    }
    return true;
  }

  std::uint8_t* data_;
  std::size_t capacity_;
  std::size_t size_{0};
  bool ok_{true};
};

// Sequential little-endian byte reader. Any short read marks the reader failed
// and leaves outputs untouched, so callers may check ok() once at the end.
class ProbeReader {
 public:
  ProbeReader(std::uint8_t const* data, std::size_t size)
      : data_{data}, size_{size} {}

  bool U8(std::uint8_t& out) {
    if (!Fits(1)) {
      return false;
    }
    out = data_[pos_++];
    return true;
  }

  bool U16(std::uint16_t& out) {
    if (!Fits(2)) {
      return false;
    }
    out = static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data_[pos_]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(data_[pos_ + 1])
                                   << 8));
    pos_ += 2;
    return true;
  }

  bool U32(std::uint32_t& out) {
    if (!Fits(4)) {
      return false;
    }
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      value |= static_cast<std::uint32_t>(data_[pos_ + static_cast<std::size_t>(
                                                          i)])
               << (8 * i);
    }
    pos_ += 4;
    out = value;
    return true;
  }

  bool I32(std::int32_t& out) {
    std::uint32_t raw = 0;
    if (!U32(raw)) {
      return false;
    }
    out = static_cast<std::int32_t>(raw);
    return true;
  }

  std::size_t consumed() const { return pos_; }
  bool ok() const { return ok_; }

 private:
  bool Fits(std::size_t need) {
    if (pos_ + need > size_) {
      ok_ = false;
      return false;
    }
    return true;
  }

  std::uint8_t const* data_;
  std::size_t size_;
  std::size_t pos_{0};
  bool ok_{true};
};

// Power-factor bench: minimal counters only (timing/energy from PPK).
struct BenchArm {
  std::uint32_t session{0};
  std::uint16_t variant_id{0};
  std::uint16_t expected{100};
};

struct BenchData {
  std::uint32_t session{0};
  std::uint16_t variant_id{0};
  std::uint16_t seq{0};
  std::uint8_t flags{0};  // bit0 sendto_ok bit1 txdone bit2 wifi_fail bit3 bad_wake
};

struct BenchSummary {
  std::uint32_t session{0};
  std::uint16_t variant_id{0};
  std::uint16_t hot_attempts{0};
  std::uint16_t sendto_ok{0};
  std::uint16_t txdone_ok{0};
  std::uint16_t wifi_fail{0};
  std::uint16_t tx_unconfirmed{0};
  std::uint16_t bad_wakes{0};
};

inline std::size_t Pack(BenchArm const& msg, std::uint8_t* out,
                        std::size_t capacity) {
  ProbeWriter w{out, capacity};
  w.U8(static_cast<std::uint8_t>(ProbeMsgType::kBenchArm));
  w.U32(msg.session);
  w.U16(msg.variant_id);
  w.U16(msg.expected);
  return w.ok() ? w.size() : 0;
}

inline bool Unpack(std::uint8_t const* data, std::size_t size, BenchArm& msg) {
  ProbeReader r{data, size};
  std::uint8_t type = 0;
  r.U8(type);
  r.U32(msg.session);
  r.U16(msg.variant_id);
  r.U16(msg.expected);
  return r.ok() && type == static_cast<std::uint8_t>(ProbeMsgType::kBenchArm);
}

inline std::size_t Pack(BenchData const& msg, std::uint8_t* out,
                        std::size_t capacity) {
  ProbeWriter w{out, capacity};
  w.U8(static_cast<std::uint8_t>(ProbeMsgType::kBenchData));
  w.U32(msg.session);
  w.U16(msg.variant_id);
  w.U16(msg.seq);
  w.U8(msg.flags);
  return w.ok() ? w.size() : 0;
}

inline bool Unpack(std::uint8_t const* data, std::size_t size, BenchData& msg) {
  ProbeReader r{data, size};
  std::uint8_t type = 0;
  r.U8(type);
  r.U32(msg.session);
  r.U16(msg.variant_id);
  r.U16(msg.seq);
  r.U8(msg.flags);
  return r.ok() && type == static_cast<std::uint8_t>(ProbeMsgType::kBenchData);
}

inline std::size_t Pack(BenchSummary const& msg, std::uint8_t* out,
                        std::size_t capacity) {
  ProbeWriter w{out, capacity};
  w.U8(static_cast<std::uint8_t>(ProbeMsgType::kBenchSummary));
  w.U32(msg.session);
  w.U16(msg.variant_id);
  w.U16(msg.hot_attempts);
  w.U16(msg.sendto_ok);
  w.U16(msg.txdone_ok);
  w.U16(msg.wifi_fail);
  w.U16(msg.tx_unconfirmed);
  w.U16(msg.bad_wakes);
  return w.ok() ? w.size() : 0;
}

inline bool Unpack(std::uint8_t const* data, std::size_t size,
                   BenchSummary& msg) {
  ProbeReader r{data, size};
  std::uint8_t type = 0;
  r.U8(type);
  r.U32(msg.session);
  r.U16(msg.variant_id);
  r.U16(msg.hot_attempts);
  r.U16(msg.sendto_ok);
  r.U16(msg.txdone_ok);
  r.U16(msg.wifi_fail);
  r.U16(msg.tx_unconfirmed);
  r.U16(msg.bad_wakes);
  return r.ok() &&
         type == static_cast<std::uint8_t>(ProbeMsgType::kBenchSummary);
}

// One probe packet of a parameter batch. Sent on the prepared UDP hot path
// during a no-sleep or sleep-confirm batch.
struct ProbeData {
  std::uint32_t session{0};
  std::uint16_t batch_id{0};
  std::uint16_t parameter_id{0};
  std::uint16_t seq{0};
  std::uint8_t stage{0};
  std::uint8_t profile{0};
  std::uint16_t pre_ms{0};
  std::uint16_t post_ms{0};
  std::uint16_t sleep_ms{0};
};

// Request for the delivery outcome of one batch. Sent over the FULL stream.
struct ProbeQuery {
  std::uint32_t session{0};
  std::uint16_t batch_id{0};
  std::uint16_t parameter_id{0};
  std::uint16_t expected{0};
};

// Receiver answer to ProbeQuery: unique/duplicate/missing counters for a batch.
struct ProbeResult {
  std::uint32_t session{0};
  std::uint16_t batch_id{0};
  std::uint16_t parameter_id{0};
  std::uint16_t expected{0};
  std::uint16_t unique{0};
  std::uint16_t dup{0};
  std::uint16_t missing{0};
};

// One measured send. Used by both the POST probe batch and the production hot
// run so the parameter is selected with exactly the packet production sends.
//
// Carries the timing of the *previous* send: a send's own cost is only known
// after it is over, and the deep sleep that follows it is only confirmed by the
// boot after that. `prev_flags` uses ProbeSampleFlag from product_probe_select.
struct HotData {
  std::uint32_t session{0};
  std::uint16_t batch_id{0};
  std::uint16_t parameter_id{0};
  std::uint16_t seq{0};
  std::uint8_t stage{0};
  std::uint8_t profile{0};
  std::uint16_t pre_ms{0};
  std::uint16_t post_ms{0};
  std::uint16_t sleep_ms{0};
  std::uint16_t prev_seq{0};
  std::uint8_t prev_status{0};
  std::uint8_t prev_flags{0};
  std::uint32_t prev_connect_us{0};
  std::uint32_t prev_cycle_us{0};
  std::uint32_t prev_encode_us{0};
  std::uint32_t prev_sendto_call_us{0};
  std::uint32_t prev_send_to_txdone_us{0};
  // Signed: the TX-done callback can run before sendto() returns.
  std::int32_t prev_txdone_minus_sendto_return_us{0};
  std::uint32_t prev_actual_post_us{0};
  std::uint32_t prev_teardown_us{0};
  std::uint32_t prev_awake_us{0};
  std::uint32_t prev_sleep_elapsed_us{0};
  std::uint32_t prev_wake_overhead_us{0};
};

// Device-side totals for a completed hot run.
struct HotSummary {
  std::uint32_t session{0};
  std::uint16_t batch_id{0};
  std::uint16_t parameter_id{0};
  std::uint8_t profile{0};
  std::uint16_t pre_ms{0};
  std::uint16_t post_ms{0};
  std::uint16_t sleep_ms{0};
  std::uint16_t hot_sent{0};
  std::uint16_t hot_fail{0};
  // Sends whose datagram left the socket but whose TX-done success was never
  // observed. The nonce is consumed, so they are neither retried nor clean.
  std::uint16_t hot_unconfirmed{0};
  std::uint8_t reprobe_count{0};
  std::uint8_t batch_invalidations{0};
};

inline std::size_t Pack(ProbeData const& msg, std::uint8_t* out,
                        std::size_t capacity) {
  ProbeWriter w{out, capacity};
  w.U8(static_cast<std::uint8_t>(ProbeMsgType::kProbeData));
  w.U32(msg.session);
  w.U16(msg.batch_id);
  w.U16(msg.parameter_id);
  w.U16(msg.seq);
  w.U8(msg.stage);
  w.U8(msg.profile);
  w.U16(msg.pre_ms);
  w.U16(msg.post_ms);
  w.U16(msg.sleep_ms);
  return w.ok() ? w.size() : 0;
}

inline bool Unpack(std::uint8_t const* data, std::size_t size, ProbeData& msg) {
  ProbeReader r{data, size};
  std::uint8_t type = 0;
  r.U8(type);
  r.U32(msg.session);
  r.U16(msg.batch_id);
  r.U16(msg.parameter_id);
  r.U16(msg.seq);
  r.U8(msg.stage);
  r.U8(msg.profile);
  r.U16(msg.pre_ms);
  r.U16(msg.post_ms);
  r.U16(msg.sleep_ms);
  return r.ok() && type == static_cast<std::uint8_t>(ProbeMsgType::kProbeData);
}

inline std::size_t Pack(ProbeQuery const& msg, std::uint8_t* out,
                        std::size_t capacity) {
  ProbeWriter w{out, capacity};
  w.U8(static_cast<std::uint8_t>(ProbeMsgType::kProbeQuery));
  w.U32(msg.session);
  w.U16(msg.batch_id);
  w.U16(msg.parameter_id);
  w.U16(msg.expected);
  return w.ok() ? w.size() : 0;
}

inline bool Unpack(std::uint8_t const* data, std::size_t size,
                   ProbeQuery& msg) {
  ProbeReader r{data, size};
  std::uint8_t type = 0;
  r.U8(type);
  r.U32(msg.session);
  r.U16(msg.batch_id);
  r.U16(msg.parameter_id);
  r.U16(msg.expected);
  return r.ok() && type == static_cast<std::uint8_t>(ProbeMsgType::kProbeQuery);
}

inline std::size_t Pack(ProbeResult const& msg, std::uint8_t* out,
                        std::size_t capacity) {
  ProbeWriter w{out, capacity};
  w.U8(static_cast<std::uint8_t>(ProbeMsgType::kProbeResult));
  w.U32(msg.session);
  w.U16(msg.batch_id);
  w.U16(msg.parameter_id);
  w.U16(msg.expected);
  w.U16(msg.unique);
  w.U16(msg.dup);
  w.U16(msg.missing);
  return w.ok() ? w.size() : 0;
}

inline bool Unpack(std::uint8_t const* data, std::size_t size,
                   ProbeResult& msg) {
  ProbeReader r{data, size};
  std::uint8_t type = 0;
  r.U8(type);
  r.U32(msg.session);
  r.U16(msg.batch_id);
  r.U16(msg.parameter_id);
  r.U16(msg.expected);
  r.U16(msg.unique);
  r.U16(msg.dup);
  r.U16(msg.missing);
  return r.ok() &&
         type == static_cast<std::uint8_t>(ProbeMsgType::kProbeResult);
}

inline std::size_t Pack(HotData const& msg, std::uint8_t* out,
                        std::size_t capacity) {
  ProbeWriter w{out, capacity};
  w.U8(static_cast<std::uint8_t>(ProbeMsgType::kHotData));
  w.U32(msg.session);
  w.U16(msg.batch_id);
  w.U16(msg.parameter_id);
  w.U16(msg.seq);
  w.U8(msg.stage);
  w.U8(msg.profile);
  w.U16(msg.pre_ms);
  w.U16(msg.post_ms);
  w.U16(msg.sleep_ms);
  w.U16(msg.prev_seq);
  w.U8(msg.prev_status);
  w.U8(msg.prev_flags);
  w.U32(msg.prev_connect_us);
  w.U32(msg.prev_cycle_us);
  w.U32(msg.prev_encode_us);
  w.U32(msg.prev_sendto_call_us);
  w.U32(msg.prev_send_to_txdone_us);
  w.I32(msg.prev_txdone_minus_sendto_return_us);
  w.U32(msg.prev_actual_post_us);
  w.U32(msg.prev_teardown_us);
  w.U32(msg.prev_awake_us);
  w.U32(msg.prev_sleep_elapsed_us);
  w.U32(msg.prev_wake_overhead_us);
  return w.ok() ? w.size() : 0;
}

inline bool Unpack(std::uint8_t const* data, std::size_t size, HotData& msg) {
  ProbeReader r{data, size};
  std::uint8_t type = 0;
  r.U8(type);
  r.U32(msg.session);
  r.U16(msg.batch_id);
  r.U16(msg.parameter_id);
  r.U16(msg.seq);
  r.U8(msg.stage);
  r.U8(msg.profile);
  r.U16(msg.pre_ms);
  r.U16(msg.post_ms);
  r.U16(msg.sleep_ms);
  r.U16(msg.prev_seq);
  r.U8(msg.prev_status);
  r.U8(msg.prev_flags);
  r.U32(msg.prev_connect_us);
  r.U32(msg.prev_cycle_us);
  r.U32(msg.prev_encode_us);
  r.U32(msg.prev_sendto_call_us);
  r.U32(msg.prev_send_to_txdone_us);
  r.I32(msg.prev_txdone_minus_sendto_return_us);
  r.U32(msg.prev_actual_post_us);
  r.U32(msg.prev_teardown_us);
  r.U32(msg.prev_awake_us);
  r.U32(msg.prev_sleep_elapsed_us);
  r.U32(msg.prev_wake_overhead_us);
  return r.ok() && type == static_cast<std::uint8_t>(ProbeMsgType::kHotData);
}

inline std::size_t Pack(HotSummary const& msg, std::uint8_t* out,
                        std::size_t capacity) {
  ProbeWriter w{out, capacity};
  w.U8(static_cast<std::uint8_t>(ProbeMsgType::kHotSummary));
  w.U32(msg.session);
  w.U16(msg.batch_id);
  w.U16(msg.parameter_id);
  w.U8(msg.profile);
  w.U16(msg.pre_ms);
  w.U16(msg.post_ms);
  w.U16(msg.sleep_ms);
  w.U16(msg.hot_sent);
  w.U16(msg.hot_fail);
  w.U16(msg.hot_unconfirmed);
  w.U8(msg.reprobe_count);
  w.U8(msg.batch_invalidations);
  return w.ok() ? w.size() : 0;
}

inline bool Unpack(std::uint8_t const* data, std::size_t size,
                   HotSummary& msg) {
  ProbeReader r{data, size};
  std::uint8_t type = 0;
  r.U8(type);
  r.U32(msg.session);
  r.U16(msg.batch_id);
  r.U16(msg.parameter_id);
  r.U8(msg.profile);
  r.U16(msg.pre_ms);
  r.U16(msg.post_ms);
  r.U16(msg.sleep_ms);
  r.U16(msg.hot_sent);
  r.U16(msg.hot_fail);
  r.U16(msg.hot_unconfirmed);
  r.U8(msg.reprobe_count);
  r.U8(msg.batch_invalidations);
  return r.ok() &&
         type == static_cast<std::uint8_t>(ProbeMsgType::kHotSummary);
}

// Counts unique, duplicate and missing sequence numbers of one batch.
//
// Sequence numbers are globally monotonic on the device, so the first observed
// value anchors a fixed 256-slot window. That is far wider than any batch and
// keeps the tracker allocation-free, which lets the firmware, the receiver and
// the host tests share it.
class BatchSeqTracker {
 public:
  static constexpr std::uint16_t kWindow = 256;

  void Reset(std::uint16_t expected) {
    expected_ = expected;
    unique_ = 0;
    dup_ = 0;
    out_of_window_ = 0;
    anchored_ = false;
    base_ = 0;
    for (auto& word : seen_) {
      word = 0;
    }
  }

  // Returns true when this sequence number had not been seen in this batch.
  bool Observe(std::uint16_t seq) {
    if (!anchored_) {
      base_ = seq;
      anchored_ = true;
    }
    auto const offset = static_cast<std::uint16_t>(seq - base_);
    if (offset >= kWindow) {
      ++out_of_window_;
      return false;
    }
    auto const word = static_cast<std::size_t>(offset / 64);
    auto const bit = static_cast<std::uint64_t>(1)
                     << static_cast<std::uint64_t>(offset % 64);
    if ((seen_[word] & bit) != 0) {
      ++dup_;
      return false;
    }
    seen_[word] |= bit;
    ++unique_;
    return true;
  }

  // The batch size only arrives with the query, after the packets themselves.
  void set_expected(std::uint16_t expected) { expected_ = expected; }

  std::uint16_t expected() const { return expected_; }
  std::uint16_t unique() const { return unique_; }
  std::uint16_t dup() const { return dup_; }
  std::uint16_t out_of_window() const { return out_of_window_; }

  std::uint16_t missing() const {
    return unique_ >= expected_ ? 0
                                : static_cast<std::uint16_t>(expected_ -
                                                             unique_);
  }

 private:
  std::uint64_t seen_[kWindow / 64]{};
  std::uint16_t expected_{0};
  std::uint16_t unique_{0};
  std::uint16_t dup_{0};
  std::uint16_t out_of_window_{0};
  std::uint16_t base_{0};
  bool anchored_{false};
};

// Message type of a received payload, or nothing recognizable.
inline bool PeekType(std::uint8_t const* data, std::size_t size,
                     ProbeMsgType& out) {
  if (data == nullptr || size == 0) {
    return false;
  }
  switch (data[0]) {
    case static_cast<std::uint8_t>(ProbeMsgType::kProbeData):
    case static_cast<std::uint8_t>(ProbeMsgType::kProbeQuery):
    case static_cast<std::uint8_t>(ProbeMsgType::kProbeResult):
    case static_cast<std::uint8_t>(ProbeMsgType::kHotData):
    case static_cast<std::uint8_t>(ProbeMsgType::kHotSummary):
    case static_cast<std::uint8_t>(ProbeMsgType::kBenchArm):
    case static_cast<std::uint8_t>(ProbeMsgType::kBenchData):
    case static_cast<std::uint8_t>(ProbeMsgType::kBenchSummary):
      out = static_cast<ProbeMsgType>(data[0]);
      return true;
    default:
      return false;
  }
}

}  // namespace ae::probe

#endif  // EXAMPLES_PROBE_RECEIVER_PROBE_PROTOCOL_H_
