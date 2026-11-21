// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_CLIENT_MESSAGES_RAW_RESOURCE_H_
#define AETHER_CLIENT_MESSAGES_RAW_RESOURCE_H_
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include "aether/client_messages/raw_send_policy.h"
namespace ae {
struct RawHead {
  unsigned flow;
  unsigned bytes;
};
// Single event-loop owner. Scheduling metadata only; the caller owns exactly
// one FIFO per flow (or one shared list with FIFO heads). No receiver clock.
class RawResource {
 public:
  using Tick = std::int64_t;
  explicit RawResource(RawResourceConfig config);
  unsigned Open(SendPriority priority, unsigned weight);
  void Close(unsigned flow);
  bool CanQueue(unsigned flow, std::size_t bytes) const;
  void Queued(unsigned flow, unsigned bytes);
  void Removed(unsigned flow, unsigned bytes);
  // Call before selection. Retires unconfirmed samples and updates pacing.
  void Update(Tick now);
  std::optional<unsigned> Choose(std::span<RawHead const> heads,
                                 Tick now) const;
  Tick Next(Tick now) const;
  std::uint64_t next_sequence() const { return sequence_ + 1; }
  void Sent(unsigned flow, unsigned bytes, Tick now);
  bool Receipt(unsigned flow, std::uint64_t sequence, Tick now);
  unsigned rate() const { return rate_; }
  unsigned in_flight() const { return flight_bytes_; }
  auto const& config() const { return config_; }

 private:
  struct Flow {
    unsigned share{}, slots{}, bytes{};
    double service{};
    Tick base_rtt{};
  };
  struct Flight {
    unsigned flow, bytes;
    Tick sent;
  };
  RawResourceConfig config_;
  std::array<Flow, 32> flows_{};
  std::map<std::uint64_t, Flight> pending_;
  std::uint64_t sequence_{};
  unsigned rate_, flight_bytes_{};
  Tick next_send_{}, last_update_{-1}, last_cut_{-1};
  Tick interval_rtt_{}, rtt_estimate_{100000};
  double extra_sum_{}, virtual_time_{};
  std::uint64_t ack_bytes_{};
  unsigned samples_{}, expired_{};
  bool probing_{true};
  unsigned all_samples_{}, all_expired_{};
  double missing_fraction_{};
};
}  // namespace ae
#endif
