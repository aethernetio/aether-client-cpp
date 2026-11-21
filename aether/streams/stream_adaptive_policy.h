// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_STREAMS_STREAM_ADAPTIVE_POLICY_H_
#define AETHER_STREAMS_STREAM_ADAPTIVE_POLICY_H_

#include <cstdint>

#include "aether-miscpp/reflect/reflect.h"
#include "aether/adaptive_datagram/adaptive_datagram_config.h"
#include "aether/clock.h"

namespace ae {

// Persistent adaptation intent. Live sessions, sequence numbers, measurements
// and peer clock offsets belong to the runtime stream, not this policy.
struct StreamAdaptivePolicy {
  AE_REFLECT_MEMBERS(max_loss_per_mille, loss_window, latency_target,
                     feedback_interval, feedback_timeout, compact_feedback,
                     control_interval, initial_rate_bytes_per_second,
                     min_rate_bytes_per_second, max_rate_bytes_per_second,
                     max_datagram_size, max_queue_packets,
                     max_in_flight_packets, max_history_packets, min_samples)

  std::uint32_t max_loss_per_mille{
      adaptive_datagram::Config{}.max_loss_per_mille};
  Duration loss_window{adaptive_datagram::Config{}.loss_window};
  Duration latency_target{adaptive_datagram::Config{}.latency_target};
  Duration feedback_interval{adaptive_datagram::Config{}.feedback_interval};
  Duration feedback_timeout{adaptive_datagram::Config{}.feedback_timeout};
  bool compact_feedback{adaptive_datagram::Config{}.compact_feedback};
  Duration control_interval{adaptive_datagram::Config{}.control_interval};
  std::uint64_t initial_rate_bytes_per_second{
      adaptive_datagram::Config{}.initial_rate_bytes_per_second};
  std::uint64_t min_rate_bytes_per_second{
      adaptive_datagram::Config{}.min_rate_bytes_per_second};
  std::uint64_t max_rate_bytes_per_second{
      adaptive_datagram::Config{}.max_rate_bytes_per_second};
  // Fixed-width counts keep saved policy independent of the host word size.
  std::uint32_t max_datagram_size{static_cast<std::uint32_t>(
      adaptive_datagram::Config{}.max_datagram_size)};
  std::uint32_t max_queue_packets{static_cast<std::uint32_t>(
      adaptive_datagram::Config{}.max_queue_packets)};
  std::uint32_t max_in_flight_packets{static_cast<std::uint32_t>(
      adaptive_datagram::Config{}.max_in_flight_packets)};
  std::uint32_t max_history_packets{static_cast<std::uint32_t>(
      adaptive_datagram::Config{}.max_history_packets)};
  std::uint32_t min_samples{adaptive_datagram::Config{}.min_samples};

  bool operator==(StreamAdaptivePolicy const&) const = default;

  [[nodiscard]] adaptive_datagram::Config ToConfig() const {
    auto config = adaptive_datagram::Config{};
    config.max_loss_per_mille = max_loss_per_mille;
    config.loss_window = loss_window;
    config.latency_target = latency_target;
    config.feedback_interval = feedback_interval;
    config.feedback_timeout = feedback_timeout;
    config.compact_feedback = compact_feedback;
    config.control_interval = control_interval;
    config.initial_rate_bytes_per_second = initial_rate_bytes_per_second;
    config.min_rate_bytes_per_second = min_rate_bytes_per_second;
    config.max_rate_bytes_per_second = max_rate_bytes_per_second;
    config.max_datagram_size = max_datagram_size;
    config.max_queue_packets = max_queue_packets;
    config.max_in_flight_packets = max_in_flight_packets;
    config.max_history_packets = max_history_packets;
    config.min_samples = min_samples;
    return config;
  }
};

}  // namespace ae
#endif  // AETHER_STREAMS_STREAM_ADAPTIVE_POLICY_H_
