/*
 * Copyright 2026 Aethernet Inc.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy at http://www.apache.org/licenses/LICENSE-2.0
 */

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "aether/adaptive_datagram/adaptive_datagram_stream.h"
#include "aether/env.h"
#include "aether/transport/emulated/emulated_datagram_socket.h"
#include "aether/transport/system_sockets/udp/udp.h"

namespace ae::recovery_scenarios {

class SimulationEnv final : public Env {
 public:
  TaskScheduler& scheduler() const { return scheduler_; }
  EventSystem& event_system() const { return events_; }

 private:
  void* find_component(EnvId id) noexcept override {
    if (id == EnvTypeId<TaskScheduler>::value) {
      return &scheduler_;
    }
    if (id == EnvTypeId<EventSystem>::value) {
      return &events_;
    }
    return nullptr;
  }
  mutable TaskScheduler scheduler_;
  mutable EventSystem events_;
};

enum class Scenario {
  kHealthy,
  kCapacityRecovery,
  kFeedbackOutage,
  kRandomBelowTarget,
  kRandomAboveTarget,
  kJitterReorder,
  kApplicationLimited,
  kUnreachableDeadline,
};

constexpr std::array kScenarios{Scenario::kHealthy,
                                Scenario::kCapacityRecovery,
                                Scenario::kFeedbackOutage,
                                Scenario::kRandomBelowTarget,
                                Scenario::kRandomAboveTarget,
                                Scenario::kJitterReorder,
                                Scenario::kApplicationLimited,
                                Scenario::kUnreachableDeadline};

char const* Name(Scenario scenario) {
  switch (scenario) {
    case Scenario::kHealthy:
      return "healthy";
    case Scenario::kCapacityRecovery:
      return "capacity_recovery";
    case Scenario::kFeedbackOutage:
      return "feedback_outage";
    case Scenario::kRandomBelowTarget:
      return "random_below_target";
    case Scenario::kRandomAboveTarget:
      return "random_above_target";
    case Scenario::kJitterReorder:
      return "jitter_reorder";
    case Scenario::kApplicationLimited:
      return "application_limited";
    case Scenario::kUnreachableDeadline:
      return "unreachable_deadline";
  }
  return "unknown";
}

struct Phase {
  char const* name;
  EmulatedDatagramProfile forward;
  EmulatedDatagramProfile reverse;
};

Phase ProfileAt(Scenario scenario, unsigned milliseconds) {
  Phase phase{"steady", {}, {}};
  phase.forward.bytes_per_second = 120000;
  phase.forward.queue_bytes = 4000;
  phase.reverse.bytes_per_second = 120000;
  phase.reverse.queue_bytes = 4000;
  switch (scenario) {
    case Scenario::kCapacityRecovery:
      if (milliseconds >= 10000 && milliseconds < 20000) {
        phase.name = "capacity_drop";
        phase.forward.bytes_per_second = 25000;
      } else if (milliseconds >= 20000) {
        phase.name = "recovery";
      } else {
        phase.name = "initial";
      }
      break;
    case Scenario::kFeedbackOutage:
      if (milliseconds >= 10000 && milliseconds < 13000) {
        phase.name = "feedback_blackhole";
        phase.reverse.blackhole = true;
      } else if (milliseconds >= 13000) {
        phase.name = "recovery";
      } else {
        phase.name = "initial";
      }
      break;
    case Scenario::kRandomBelowTarget:
      phase.forward.loss_per_mille = 20;
      break;
    case Scenario::kRandomAboveTarget:
      phase.forward.loss_per_mille = 100;
      break;
    case Scenario::kJitterReorder:
      phase.forward.jitter = Duration{15ms};
      phase.forward.reorder_per_mille = 50;
      phase.forward.reorder_delay = Duration{20ms};
      phase.forward.duplicate_per_mille = 30;
      break;
    case Scenario::kUnreachableDeadline:
      phase.forward.delay = Duration{150ms};
      break;
    case Scenario::kHealthy:
    case Scenario::kApplicationLimited:
      break;
  }
  return phase;
}

std::uint64_t Micros(TimePoint time) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          time.time_since_epoch())
          .count());
}

std::uint64_t Drops(EmulatedDatagramStats const& stats) {
  return stats.dropped_random + stats.dropped_queue + stats.dropped_burst +
         stats.dropped_blackhole + stats.dropped_disconnected;
}

std::uint32_t Percentile95(std::vector<std::uint32_t>& samples) {
  if (samples.empty()) {
    return 0;
  }
  std::sort(samples.begin(), samples.end());
  return samples[(samples.size() * 95 + 99) / 100 - 1];
}

// This harness deliberately uses only the public interface available at
// 71a83ee9. Compile the identical source against both libraries for A/B
// results.
class Simulation {
 public:
  Simulation(Scenario scenario, std::uint64_t seed)
      : scenario_{scenario},
        seed_{seed},
        network_{seed},
        sender_udp_{environment_, AddressPort{},
                    EmulatedDatagramSocket{network_, 0}},
        receiver_udp_{environment_, AddressPort{},
                      EmulatedDatagramSocket{network_, 1}},
        sender_{environment_, SenderConfig(),
                [this] { return network_.now(); }},
        receiver_{environment_, ReceiverConfig(),
                  [this] { return network_.now(); }} {
    sender_.LinkOut(sender_udp_);
    receiver_.LinkOut(receiver_udp_);
    received_subscription_ =
        receiver_.out_data_event().Subscribe([this](DataBuffer const& data) {
          if (data.size() != kPayloadSize) {
            sane_ = false;
            return;
          }
          std::uint64_t created = 0;
          std::memcpy(&created, data.data(), sizeof(created));
          auto const now = Micros(network_.now());
          if (created > now || now - created > kLatencyTargetUs) {
            sane_ = false;
            return;
          }
          auto const latency = static_cast<std::uint32_t>(now - created);
          received_bytes_ += data.size();
          ++received_packets_;
          second_latencies_.push_back(latency);
          if (elapsed_ms_ >= duration_ms_ - 5000) {
            tail_latencies_.push_back(latency);
          }
        });
  }

  bool Run(std::ostream& csv, bool check_performance = false) {
    if (!sender_.valid() || !receiver_.valid()) {
      return false;
    }
    duration_ms_ = scenario_ == Scenario::kCapacityRecovery ? 36000U : 30000U;
    auto next_write = network_.now();
    std::uint64_t previous_received = 0;
    std::uint64_t tail_bytes = 0;
    std::uint64_t offered_rate = 0;
    unsigned violation_rows = 0;
    unsigned incomplete_rows = 0;
    auto const initial_phase = ProfileAt(scenario_, 0);
    network_.SetProfile(0, initial_phase.forward);
    network_.SetProfile(1, initial_phase.reverse);
    auto previous_phase = std::string_view{initial_phase.name};
    for (elapsed_ms_ = 0; elapsed_ms_ < duration_ms_; ++elapsed_ms_) {
      auto const phase = ProfileAt(scenario_, elapsed_ms_);
      if (std::string_view{phase.name} != previous_phase) {
        network_.SetProfile(0, phase.forward);
        network_.SetProfile(1, phase.reverse);
        previous_phase = phase.name;
      }
      if (network_.now() >= next_write) {
        auto const allowance = sender_.rate_bytes_per_second();
        offered_rate = scenario_ == Scenario::kApplicationLimited
                           ? 10000U
                           : allowance - allowance / 5;
        auto const interval_us =
            (kPayloadSize * 1000000ULL + offered_rate - 1) / offered_rate;
        next_write =
            network_.now() +
            std::chrono::microseconds{
                static_cast<std::chrono::microseconds::rep>(interval_us)};
        if (sender_.stream_info().is_writable) {
          DataBuffer data(kPayloadSize, 0);
          auto const created = Micros(network_.now());
          std::memcpy(data.data(), &created, sizeof(created));
          sender_.Write(std::move(data));
        }
      }
      network_.AdvanceTo(network_.now() + 1ms);
      for (int pump = 0; pump < 4; ++pump) {
        environment_.scheduler().Update(network_.now());
      }
      auto const stats = sender_.statistics();
      if (stats.queued > 128 || stats.in_flight > 512 ||
          environment_.scheduler().overflow_counter() != 0) {
        sane_ = false;
      }
      if ((elapsed_ms_ + 1) % 1000 != 0) {
        continue;
      }
      auto const& metrics = sender_.metrics();
      auto const goodput = received_bytes_ - previous_received;
      previous_received = received_bytes_;
      if (elapsed_ms_ >= duration_ms_ - 5000) {
        tail_bytes += goodput;
      }
      if (metrics.window_valid && metrics.window_loss_per_mille > 50) {
        ++violation_rows;
      }
      if (metrics.feedback_incomplete) {
        ++incomplete_rows;
      }
      csv << Name(scenario_) << ',' << seed_ << ',' << (elapsed_ms_ + 1) / 1000
          << ',' << phase.name << ',' << phase.forward.bytes_per_second << ','
          << offered_rate << ',' << goodput << ','
          << sender_.rate_bytes_per_second() << ','
          << metrics.window_loss_per_mille << ',' << metrics.window_valid << ','
          << metrics.feedback_incomplete << ',' << metrics.constraints_unmet
          << ',' << metrics.latest_latency.count() << ','
          << Percentile95(second_latencies_) << ',' << metrics.feedback_stale
          << ',' << stats.queued << ',' << stats.in_flight << ','
          << received_packets_ << ',' << received_bytes_ << ','
          << stats.delivered << ',' << stats.lost << ',' << stats.unconfirmed
          << ',' << stats.late << ',' << stats.expired << ','
          << Drops(network_.stats(0)) + Drops(network_.stats(1)) << '\n';
      second_latencies_.clear();
      if (!csv) {
        return false;
      }
    }
    auto const capacity =
        ProfileAt(scenario_, duration_ms_ - 1).forward.bytes_per_second;
    auto const tail_goodput = tail_bytes / 5;
    if (check_performance) {
      std::uint64_t minimum = 0;
      switch (scenario_) {
        case Scenario::kHealthy:
        case Scenario::kCapacityRecovery:
          minimum = 90000;
          break;
        case Scenario::kFeedbackOutage:
        case Scenario::kJitterReorder:
          minimum = 85000;
          break;
        case Scenario::kRandomBelowTarget:
          minimum = 70000;
          break;
        case Scenario::kApplicationLimited:
          minimum = 9500;
          break;
        default:
          break;  // Above-L random losses cannot be repaired by rate alone.
      }
      if (tail_goodput < minimum) {
        std::cerr << "Recovery/throughput regression in " << Name(scenario_)
                  << ": " << tail_goodput << " < " << minimum << '\n';
        sane_ = false;
      }
    }
    std::cout << Name(scenario_) << " seed=" << seed_
              << " tail_goodput_Bps=" << tail_goodput
              << " tail_wire_capacity_util_permille="
              << tail_goodput * 1000 / capacity
              << " tail_app_latency_p95_us=" << Percentile95(tail_latencies_)
              << " valid_loss_violation_rows=" << violation_rows
              << " incomplete_rows=" << incomplete_rows
              << " delivered_bytes=" << received_bytes_ << '\n';
    sender_.Unlink();
    receiver_.Unlink();
    return sane_;
  }

 private:
  static adaptive_datagram::Config SenderConfig() {
    adaptive_datagram::Config config;
    config.session_id = 1;
    config.peer_session_id = 2;
    return config;
  }
  static adaptive_datagram::Config ReceiverConfig() {
    auto config = SenderConfig();
    config.session_id = 2;
    config.peer_session_id = 1;
    return config;
  }

  static constexpr std::size_t kPayloadSize = 800;
  static constexpr std::uint64_t kLatencyTargetUs = 100000;
  Scenario scenario_;
  std::uint64_t seed_;
  SimulationEnv environment_;
  EmulatedDatagramNetwork network_;
  UdpTransport<EmulatedDatagramSocket, 16> sender_udp_;
  UdpTransport<EmulatedDatagramSocket, 16> receiver_udp_;
  AdaptiveDatagramStream sender_;
  AdaptiveDatagramStream receiver_;
  Subscription received_subscription_;
  unsigned duration_ms_{30000};
  unsigned elapsed_ms_{0};
  bool sane_{true};
  std::uint64_t received_bytes_{0};
  std::uint64_t received_packets_{0};
  std::vector<std::uint32_t> second_latencies_;
  std::vector<std::uint32_t> tail_latencies_;
};
}  // namespace ae::recovery_scenarios

int main(int argc, char* argv[]) {
  if (argc > 4 || (argc == 4 && std::string_view{argv[3]} != "--check")) {
    std::cerr
        << "Usage: adaptive-datagram-recovery [output.csv] [seed] [--check]\n";
    return 2;
  }
  auto const* output = argc >= 2 ? argv[1] : "adaptive-recovery.csv";
  std::array<std::uint64_t, 3> seeds{12345, 24680, 7};
  std::size_t seed_count = seeds.size();
  if (argc >= 3) {
    std::string_view text{argv[2]};
    auto const parsed =
        std::from_chars(text.data(), text.data() + text.size(), seeds[0]);
    if (text.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size()) {
      std::cerr << "Seed must be an unsigned decimal integer.\n";
      return 2;
    }
    seed_count = 1;
  }
  if (std::string_view{output}.empty()) {
    return 2;
  }
  std::ofstream csv{output, std::ios::out | std::ios::trunc};
  if (!csv) {
    std::cerr << "Cannot open CSV output.\n";
    return 1;
  }
  csv << "scenario,seed,simulated_sec,phase,capacity_Bps,offered_Bps,goodput_"
         "Bps,"
         "allowance_Bps,window_loss_permille,window_valid,feedback_incomplete,"
         "constraints_unmet,latest_latency_us,app_latency_p95_us,feedback_"
         "stale,"
         "queue_packets,inflight,received_packets,received_bytes,delivered,"
         "lost,"
         "unconfirmed,late,expired,network_drops\n";
  for (std::size_t seed = 0; seed < seed_count; ++seed) {
    for (auto scenario : ae::recovery_scenarios::kScenarios) {
      ae::recovery_scenarios::Simulation simulation{scenario, seeds[seed]};
      if (!simulation.Run(csv, argc == 4)) {
        std::cerr
            << "Output failure, deadline violation, or resource overflow.\n";
        return 1;
      }
    }
  }
  csv.flush();
  return csv ? 0 : 1;
}
