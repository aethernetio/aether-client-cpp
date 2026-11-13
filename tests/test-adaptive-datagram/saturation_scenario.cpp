/*
 * Copyright 2026 Aethernet Inc.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy at http://www.apache.org/licenses/LICENSE-2.0
 */
#include "saturation_scenario.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include "aether/transport/emulated/emulated_datagram_socket.h"
#include "aether/types/address.h"
#include "aether/types/data_buffer.h"
#include "aether/types/span.h"

namespace ae::saturation {
namespace saturation_scenario_internal {

bool Valid(Config const& config) {
  return config.capacity_Bps > 0 && config.capacity_Bps <= 1000000000ULL &&
         config.packet_bytes >= 16 && config.packet_bytes <= 65535 &&
         config.queue_packets > 0 && config.queue_packets <= 65536 &&
         config.packet_bytes <= 64 * 1024 * 1024 / config.queue_packets &&
         config.load_per_mille > 0 && config.load_per_mille <= 10000 &&
         config.measure_ms > 0 && config.measure_ms <= 3600000 &&
         config.warmup_ms <= 3600000;
}

std::uint64_t Random(std::uint64_t& state) {
  state ^= state >> 12;
  state ^= state << 25;
  state ^= state >> 27;
  return state * 0x2545f4914f6cdd1dULL;
}

std::uint64_t OtherDrops(EmulatedDatagramStats const& stats) {
  return stats.dropped_burst + stats.dropped_blackhole +
         stats.dropped_disconnected;
}

std::uint32_t Percentile(std::vector<std::uint32_t> const& sorted,
                         std::size_t percent) {
  if (sorted.empty()) {
    return 0;
  }
  // Nearest-rank percentile, including the full measured cohort after drain.
  auto const index = (sorted.size() * percent + 99) / 100 - 1;
  return sorted[index];
}

}  // namespace saturation_scenario_internal

Result Run(Config const& config) {
  Result result;
  assert(saturation_scenario_internal::Valid(config) &&
         "Saturation scenario configuration must be valid");
  if (!saturation_scenario_internal::Valid(config)) {
    return result;
  }
  result.local_writes_ok = true;
  std::vector<std::uint32_t> delays;
  EmulatedDatagramNetwork network{config.seed};
  EmulatedDatagramSocket sender{network, 0};
  EmulatedDatagramSocket receiver{network, 1};
  EmulatedDatagramProfile profile;
  profile.bytes_per_second = config.capacity_Bps;
  profile.queue_bytes = config.packet_bytes * config.queue_packets;
  profile.mtu = config.packet_bytes;
  profile.delay = config.propagation_delay;
  // All other profile fields retain zero random loss/jitter/reordering/dupes.
  network.SetProfile(0, profile);
  sender.Connect(AddressPort{}, {});
  receiver.Connect(AddressPort{}, {});

  struct ReceiveContext {
    EmulatedDatagramNetwork* network;
    Result* result;
    std::vector<std::uint32_t>* delays;
  } receive_context{&network, &result, &delays};
  receiver.RecvData([&receive_context](Span<std::uint8_t> data) {
    if (data.size() < 9) {
      receive_context.result->local_writes_ok = false;
      return;
    }
    if (data.data()[8] == 0) {
      return;  // Warmup packet; it still consumed real queue capacity.
    }
    std::uint64_t sent_us = 0;
    std::memcpy(&sent_us, data.data(), sizeof(sent_us));
    auto const received_us = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            receive_context.network->now().time_since_epoch())
            .count());
    assert(received_us >= sent_us && "Packet arrived before submission");
    auto const elapsed = received_us - sent_us;
    receive_context.delays->push_back(
        static_cast<std::uint32_t>(std::min<std::uint64_t>(
            elapsed, std::numeric_limits<std::uint32_t>::max())));
    ++receive_context.result->received_packets;
    receive_context.result->received_bytes += data.size();
  });

  auto const warmup_us = static_cast<std::uint64_t>(config.warmup_ms) * 1000;
  auto const end_us =
      warmup_us + static_cast<std::uint64_t>(config.measure_ms) * 1000;
  auto const average_interval_us =
      static_cast<long double>(config.packet_bytes) * 1000000000.0L /
      (static_cast<long double>(config.capacity_Bps) *
       static_cast<long double>(config.load_per_mille));
  auto random_state = config.seed == 0 ? std::uint64_t{1} : config.seed;
  long double next_arrival_us = 0;
  DataBuffer packet(config.packet_bytes, 0);
  while (next_arrival_us < static_cast<long double>(end_us)) {
    auto const sent_us = static_cast<std::uint64_t>(next_arrival_us);
    network.AdvanceTo(TimePoint{} + std::chrono::microseconds{sent_us});
    auto const measured = sent_us >= warmup_us;
    std::memcpy(packet.data(), &sent_us, sizeof(sent_us));
    packet[8] = measured ? 1 : 0;
    auto const before = network.stats(0);
    auto sent = sender.Send(Span{packet.data(), packet.size()});
    if (!sent || *sent != packet.size()) {
      result.local_writes_ok = false;
    }
    if (measured) {
      auto const& after = network.stats(0);
      ++result.offered_packets;
      result.offered_bytes += packet.size();
      result.queue_drops += after.dropped_queue - before.dropped_queue;
      result.random_drops += after.dropped_random - before.dropped_random;
      result.other_drops += saturation_scenario_internal::OtherDrops(after) -
                            saturation_scenario_internal::OtherDrops(before);
    }

    auto interval = average_interval_us;
    if (config.pattern == ArrivalPattern::kPoisson) {
      auto const bits =
          saturation_scenario_internal::Random(random_state) >> 12;
      // Strictly between zero and one; no infinity or zero-spacing log result.
      auto const uniform =
          (static_cast<long double>(bits) + 0.5L) / 4503599627370496.0L;
      interval *= -std::log(uniform);
    }
    next_arrival_us += interval;
  }

  auto const serialization_us =
      (static_cast<std::uint64_t>(config.packet_bytes) * 1000000ULL +
       config.capacity_Bps - 1) /
      config.capacity_Bps;
  auto const drain_until_us = end_us + config.propagation_delay.count() +
                              (config.queue_packets + 1) * serialization_us + 1;
  network.AdvanceTo(TimePoint{} + std::chrono::microseconds{drain_until_us});
  std::sort(delays.begin(), delays.end());
  result.delay_p50_us = saturation_scenario_internal::Percentile(delays, 50);
  result.delay_p95_us = saturation_scenario_internal::Percentile(delays, 95);
  result.delay_max_us = delays.empty() ? 0 : delays.back();
  result.peak_queue_bytes = network.stats(0).peak_queue_bytes;
  auto const seconds = static_cast<double>(config.measure_ms) / 1000.0;
  result.offered_Bps = static_cast<double>(result.offered_bytes) / seconds;
  result.goodput_Bps = static_cast<double>(result.received_bytes) / seconds;
  result.loss_pct =
      result.offered_packets == 0
          ? 0.0
          : static_cast<double>(result.queue_drops + result.random_drops +
                                result.other_drops) *
                100.0 / static_cast<double>(result.offered_packets);
  result.accounting_ok =
      result.offered_packets == result.received_packets + result.queue_drops +
                                    result.random_drops + result.other_drops;
  return result;
}

}  // namespace ae::saturation
