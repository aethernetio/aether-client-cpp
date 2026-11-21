// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include "aether/client_messages/raw_resource.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace ae {
namespace {
void Append64(DataBuffer& data, std::uint64_t value) {
  for (unsigned i = 0; i < 8; ++i) {
    data.push_back(static_cast<std::uint8_t>(value >> (i * 8)));
  }
}
std::uint64_t Read64(std::span<std::uint8_t const> data) {
  std::uint64_t value{};
  for (unsigned i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(data[i]) << (i * 8);
  }
  return value;
}
}  // namespace
DataBuffer RawFrame(RawReceipt tag, std::span<std::uint8_t const> payload) {
  DataBuffer result;
  result.reserve(kRawHeaderBytes + payload.size());
  result.push_back(0x10);
  Append64(result, tag.epoch);
  Append64(result, tag.sequence);
  result.insert(result.end(), payload.begin(), payload.end());
  return result;
}
DataBuffer RawAck(RawReceipt tag) {
  auto result = RawFrame(tag, {});
  result.front() = 0x11;
  return result;
}
std::optional<RawReceipt> ReadRawHeader(std::span<std::uint8_t const> data,
                                        bool ack) {
  if (data.size() < kRawHeaderBytes || data.front() != (ack ? 0x11 : 0x10) ||
      (ack && data.size() != kRawHeaderBytes)) {
    return {};
  }
  return RawReceipt{Read64(data.subspan(1)), Read64(data.subspan(9))};
}
RawResource::RawResource(RawResourceConfig config)
    : config_{config}, rate_{config.initial_rate} {
  if (!config.max_flows || config.max_flows > flows_.size() ||
      !config.slots_per_flow || config.slots_per_flow > 64 / config.max_flows ||
      !config.bytes_per_flow || !config.flight_bytes ||
      !config.flight_packets || !config.min_rate || config.min_rate > rate_ ||
      rate_ > config.max_rate || !config.target_extra_rtt_us ||
      !config.receipt_timeout_us || !config.queue_lifetime_us) {
    throw std::invalid_argument("Invalid Raw resource bounds");
  }
}
unsigned RawResource::Open(SendPriority priority, unsigned weight) {
  if (weight == 0 || weight > 16) {
    throw std::invalid_argument("Invalid Raw flow weight");
  }
  unsigned rank = static_cast<unsigned>(priority);
  unsigned share = rank < 64 ? 8u : rank < 128 ? 4u : rank < 255 ? 2u : 1u;
  for (unsigned i = 0; i < config_.max_flows; ++i) {
    if (!flows_[i].share) {
      flows_[i] = Flow{share * weight, 0, 0, virtual_time_, 0};
      return i;
    }
  }
  throw std::length_error("Raw resource flow slots exhausted");
}
void RawResource::Close(unsigned flow) {
  flows_.at(flow) = {};
  // Keep credit charged to the resource until expiry, even across reconnects.
  for (auto& [seq, item] : pending_) {
    if (item.flow == flow) {
      item.flow = static_cast<unsigned>(flows_.size());
    }
  }
}
bool RawResource::CanQueue(unsigned flow, std::size_t bytes) const {
  auto const& f = flows_.at(flow);
  return f.share && bytes && f.slots < config_.slots_per_flow &&
         bytes <= config_.bytes_per_flow - f.bytes &&
         bytes <= config_.flight_bytes;
}
void RawResource::Queued(unsigned flow, unsigned bytes) {
  assert(CanQueue(flow, bytes) && "Raw queue admission exceeded");
  auto& f = flows_[flow];
  if (!f.slots) {
    f.service = std::max(virtual_time_, f.service);
  }
  ++f.slots;
  f.bytes += bytes;
}
void RawResource::Removed(unsigned flow, unsigned bytes) {
  auto& f = flows_.at(flow);
  assert(f.slots && f.bytes >= bytes && "Unbalanced Raw queue removal");
  --f.slots;
  f.bytes -= bytes;
}
std::optional<unsigned> RawResource::Choose(std::span<RawHead const> heads,
                                            Tick now) const {
  if (now < next_send_ || pending_.size() >= config_.flight_packets) {
    return {};
  }
  double best = std::numeric_limits<double>::max();
  std::optional<unsigned> winner;
  unsigned cost{};
  for (auto h : heads) {
    auto const& f = flows_.at(h.flow);
    double finish = f.service + static_cast<double>(h.bytes) / f.share;
    if (finish < best) {
      best = finish;
      winner = h.flow;
      cost = h.bytes;
    }
  }
  if (cost > config_.flight_bytes - flight_bytes_) {
    return {};
  }
  // ACK clock: a rate cut alone cannot recall a full BDP after a capacity step.
  // Reserve measured missing-receipt volume so random erasures do not
  // permanently consume the useful window. The hard bound always wins.
  Tick base{};
  for (auto const& f : flows_) {
    base = std::max(base, f.base_rtt);
  }
  if (base) {
    double horizon = static_cast<double>(base + config_.target_extra_rtt_us) +
                     missing_fraction_ * config_.receipt_timeout_us;
    double window = 2.0 * cost + rate_ * horizon / 1000000.0;
    if (static_cast<double>(flight_bytes_) + cost > window) {
      return {};
    }
  }
  return winner;
}
void RawResource::Sent(unsigned flow, unsigned bytes, Tick now) {
  auto& f = flows_[flow];
  virtual_time_ = std::max(virtual_time_, f.service);
  f.service += static_cast<double>(bytes) / f.share;
  pending_.emplace(++sequence_, Flight{flow, bytes, now});
  flight_bytes_ += bytes;
  auto spacing = static_cast<Tick>((1000000ULL * bytes + rate_ - 1) / rate_);
  next_send_ =
      std::max(next_send_, now - std::min<Tick>(200, spacing / 2)) + spacing;
}
bool RawResource::Receipt(unsigned flow, std::uint64_t seq, Tick now) {
  if (flow >= config_.max_flows || !flows_[flow].share) {
    return false;
  }
  auto it = pending_.find(seq);
  if (it == pending_.end() || it->second.flow != flow) {
    return false;
  }
  auto const item = it->second;
  if (now < item.sent || now - item.sent >= config_.receipt_timeout_us) {
    return false;
  }
  flight_bytes_ -= item.bytes;
  pending_.erase(it);
  auto& f = flows_[flow];
  auto rtt = std::max<Tick>(1, now - item.sent);
  if (!f.base_rtt || rtt < f.base_rtt) {
    f.base_rtt = rtt;
  }
  ack_bytes_ += item.bytes;
  ++all_samples_;
  if (item.sent >= last_cut_) {
    extra_sum_ += static_cast<double>(rtt - f.base_rtt);
    interval_rtt_ = std::max(interval_rtt_, rtt);
    ++samples_;
  }
  return true;
}
void RawResource::Update(Tick now) {
  while (!pending_.empty() &&
         now - pending_.begin()->second.sent >= config_.receipt_timeout_us) {
    auto const item = pending_.begin()->second;
    flight_bytes_ -= item.bytes;
    if (item.flow < flows_.size()) {
      ++all_expired_;
      if (item.sent >= last_cut_) {
        ++expired_;
      }
    }
    pending_.erase(pending_.begin());
  }
  if (last_update_ < 0) {
    last_update_ = now;
  }
  auto elapsed = now - last_update_;
  if (elapsed < std::clamp<Tick>(rtt_estimate_, 100000, 500000)) {
    return;
  }
  double observed = static_cast<double>(ack_bytes_) * 1000000.0 /
                    static_cast<double>(elapsed);
  if (all_samples_ + all_expired_ >= 8) {
    double fraction =
        static_cast<double>(all_expired_) / (all_samples_ + all_expired_);
    missing_fraction_ = 0.8 * missing_fraction_ + 0.2 * fraction;
  }
  all_samples_ = all_expired_ = 0;
  double excess = samples_ ? extra_sum_ / samples_ : 0;
  bool delay = samples_ && excess > config_.target_extra_rtt_us;
  bool stale =
      expired_ > 0 && expired_ > samples_ / 5 && observed < rate_ * 0.55;
  if (delay || stale) {
    probing_ = false;
    // Count ALL bytes returned in the interval when estimating throughput.
    // Delay decisions use only data sent since the last reduction. Old ACKs
    // draining a queue are evidence of capacity even after the pacing cut.
    double candidate = rate_;
    if (stale) {
      candidate = rate_ * 0.5;
    } else {
      double drain =
          std::clamp(config_.target_extra_rtt_us / excess, 0.55, 0.96);
      candidate =
          std::max(rate_ * 0.35, std::min<double>(rate_, observed * drain));
    }
    if (candidate < rate_) {
      rate_ =
          static_cast<unsigned>(std::max<double>(config_.min_rate, candidate));
      last_cut_ = now;
    }
  } else if (samples_ && excess < config_.target_extra_rtt_us * 0.5 &&
             observed >= rate_ * 0.72) {
    rate_ = static_cast<unsigned>(std::min<double>(
        config_.max_rate, rate_ * (probing_ ? 1.12 : 1.04) + 256));
  }
  if (samples_) {
    rtt_estimate_ = (3 * rtt_estimate_ + interval_rtt_) / 4;
  }
  samples_ = expired_ = 0;
  ack_bytes_ = 0;
  extra_sum_ = 0;
  interval_rtt_ = 0;
  last_update_ = now;
}
RawResource::Tick RawResource::Next(Tick now) const {
  if (next_send_ > now) {
    return next_send_;
  }
  if (!pending_.empty()) {
    return pending_.begin()->second.sent + config_.receipt_timeout_us;
  }
  return now;
}
}  // namespace ae
