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

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string_view>
#include <system_error>

#include "aether-objects/env/env.h"

#include "aether/adaptive_datagram/adaptive_datagram_config.h"
#include "aether/adaptive_datagram/adaptive_datagram_stream.h"
#include "aether/ae_context.h"
#include "aether/clock.h"
#include "aether/events/events.h"
#include "aether/tasks/manual_task_scheduler.h"
#include "aether/transport/emulated/emulated_datagram_socket.h"
#include "aether/transport/system_sockets/sockets/isocket.h"
#include "aether/transport/system_sockets/udp/udp.h"
#include "aether/types/data_buffer.h"

namespace ae::adaptive_datagram_emulation {

// A standalone runtime environment: the example needs no persistent objects,
// real network adapters, server credentials, or test-only context headers.
class SimulationEnv final : public Env {
 public:
  TaskScheduler& scheduler() const { return scheduler_; }
  EventSystem& event_system() const { return event_system_; }

 private:
  void* find_component(EnvId id) noexcept override {
    if (id == EnvTypeId<TaskScheduler>::value) {
      return &scheduler_;
    }
    if (id == EnvTypeId<EventSystem>::value) {
      return &event_system_;
    }
    return nullptr;
  }

  mutable TaskScheduler scheduler_;
  mutable EventSystem event_system_;
};

enum class Phase {
  kHealthy,
  kCapacityDrop,
  kJitterAndRandomLoss,
  kBurstLoss,
  kFeedbackBlackhole,
  kRecovery,
};

Phase PhaseAt(std::uint32_t elapsed_ms) {
  if (elapsed_ms < 8000) {
    return Phase::kHealthy;
  }
  if (elapsed_ms < 16000) {
    return Phase::kCapacityDrop;
  }
  if (elapsed_ms < 24000) {
    return Phase::kJitterAndRandomLoss;
  }
  if (elapsed_ms < 28000) {
    return Phase::kBurstLoss;
  }
  if (elapsed_ms < 31000) {
    return Phase::kFeedbackBlackhole;
  }
  return Phase::kRecovery;
}

char const* PhaseName(Phase phase) {
  switch (phase) {
    case Phase::kHealthy:
      return "healthy";
    case Phase::kCapacityDrop:
      return "capacity_drop";
    case Phase::kJitterAndRandomLoss:
      return "jitter_random_loss";
    case Phase::kBurstLoss:
      return "burst_loss";
    case Phase::kFeedbackBlackhole:
      return "feedback_blackhole";
    case Phase::kRecovery:
      return "recovery";
  }
  return "unknown";
}

void ApplyPhase(EmulatedDatagramNetwork& network, Phase phase) {
  EmulatedDatagramProfile forward;
  forward.delay = Duration{10ms};
  forward.bytes_per_second = 120000;
  forward.queue_bytes = 4000;
  switch (phase) {
    case Phase::kHealthy:
    case Phase::kRecovery:
      break;
    case Phase::kCapacityDrop:
      forward.bytes_per_second = 25000;
      break;
    case Phase::kJitterAndRandomLoss:
      forward.bytes_per_second = 100000;
      forward.jitter = Duration{15ms};
      forward.loss_per_mille = 20;
      forward.duplicate_per_mille = 30;
      forward.reorder_per_mille = 50;
      forward.reorder_delay = Duration{20ms};
      break;
    case Phase::kBurstLoss:
      forward.bytes_per_second = 100000;
      forward.burst_every = 20;
      forward.burst_length = 2;
      break;
    case Phase::kFeedbackBlackhole:
      forward.bytes_per_second = 100000;
      break;
  }
  EmulatedDatagramProfile reverse;
  reverse.delay = Duration{10ms};
  reverse.bytes_per_second = 120000;
  reverse.queue_bytes = 4000;
  reverse.blackhole = phase == Phase::kFeedbackBlackhole;
  network.SetProfile(0, forward);
  network.SetProfile(1, reverse);
}

std::uint64_t NetworkDrops(EmulatedDatagramStats const& stats) {
  return stats.dropped_random + stats.dropped_queue + stats.dropped_burst +
         stats.dropped_blackhole + stats.dropped_disconnected;
}

class Simulation {
 public:
  explicit Simulation(std::uint64_t seed)
      : network_{seed},
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
    receiver_data_ = receiver_.out_data_event().Subscribe(
        [this](DataBuffer const& data) { received_bytes_ += data.size(); });
    ApplyPhase(network_, Phase::kHealthy);
    PumpScheduler();
  }

  bool Run(std::ofstream& csv) {
    if (!sender_.valid() || !receiver_.valid()) {
      std::cerr << "Invalid adaptive datagram configuration.\n";
      return false;
    }
    csv << "simulated_sec,phase,wire_rate_Bps,goodput_Bps,"
           "window_loss_permille,window_valid,feedback_incomplete,constraints_"
           "unmet,"
           "latest_latency_us,feedback_stale,"
           "queue_packets,inflight,expired,lost,unconfirmed,late,network_"
           "drops\n";
    auto next_write = network_.now();
    std::uint64_t previous_received_bytes = 0;
    auto phase = Phase::kHealthy;
    for (std::uint32_t elapsed_ms = 0; elapsed_ms < 40000; ++elapsed_ms) {
      auto const next_phase = PhaseAt(elapsed_ms);
      if (next_phase != phase) {
        phase = next_phase;
        ApplyPhase(network_, phase);
      }

      if (network_.now() >= next_write) {
        // Offer 80% of the current allowance, with no catch-up burst when the
        // lower stream applies backpressure. The stream still owns wire pacing.
        auto const rate = sender_.rate_bytes_per_second();
        auto const producer_rate = rate - rate / 5;
        auto const interval_us =
            (kPayloadSize * 1000000ULL + producer_rate - 1) / producer_rate;
        next_write =
            network_.now() +
            std::chrono::microseconds{
                static_cast<std::chrono::microseconds::rep>(interval_us)};
        if (sender_.stream_info().is_writable) {
          sender_.Write(DataBuffer(
              kPayloadSize, static_cast<std::uint8_t>(elapsed_ms % 256)));
        }
      }

      network_.AdvanceTo(network_.now() + 1ms);
      PumpScheduler();
      if ((elapsed_ms + 1) % 1000 == 0) {
        auto const stats = sender_.statistics();
        auto const& metrics = sender_.metrics();
        csv << (elapsed_ms + 1) / 1000 << ',' << PhaseName(phase) << ','
            << sender_.rate_bytes_per_second() << ','
            << received_bytes_ - previous_received_bytes << ','
            << metrics.window_loss_per_mille << ',' << metrics.window_valid
            << ',' << metrics.feedback_incomplete << ','
            << metrics.constraints_unmet << ','
            << metrics.latest_latency.count() << ',' << metrics.feedback_stale
            << ',' << stats.queued << ',' << stats.in_flight << ','
            << stats.expired << ',' << stats.lost << ',' << stats.unconfirmed
            << ',' << stats.late << ','
            << NetworkDrops(network_.stats(0)) + NetworkDrops(network_.stats(1))
            << '\n';
        previous_received_bytes = received_bytes_;
        if (!csv) {
          std::cerr << "Failed to write the simulation CSV.\n";
          return false;
        }
      }
    }
    csv.flush();
    if (!csv) {
      std::cerr << "Failed to flush the simulation CSV.\n";
      return false;
    }
    auto const stats = sender_.statistics();
    std::cout << "40 virtual seconds; received " << received_bytes_
              << " useful bytes; final allowance "
              << sender_.rate_bytes_per_second() << " B/s.\n"
              << "Finalized delivered/lost/unconfirmed/late/expired: "
              << stats.delivered << '/' << stats.lost << '/'
              << stats.unconfirmed << '/' << stats.late << '/' << stats.expired
              << "; still pending: " << stats.queued + stats.in_flight << ".\n";
    auto const no_overflow = environment_.scheduler().overflow_counter() == 0;
    if (!no_overflow) {
      std::cerr << "Simulation scheduler capacity was exceeded.\n";
    }
    sender_.Unlink();
    receiver_.Unlink();
    return no_overflow;
  }

 private:
  static adaptive_datagram::Config SenderConfig() {
    adaptive_datagram::Config config;
    config.session_id = 1;
    return config;
  }
  static adaptive_datagram::Config ReceiverConfig() {
    auto config = SenderConfig();
    config.session_id = 2;
    return config;
  }
  void PumpScheduler() {
    for (int i = 0; i < 4; ++i) {
      environment_.scheduler().Update(network_.now());
    }
  }

  static constexpr std::size_t kPayloadSize = 800;
  SimulationEnv environment_;
  EmulatedDatagramNetwork network_;
  UdpTransport<EmulatedDatagramSocket, 16> sender_udp_;
  UdpTransport<EmulatedDatagramSocket, 16> receiver_udp_;
  AdaptiveDatagramStream sender_;
  AdaptiveDatagramStream receiver_;
  Subscription receiver_data_;
  std::uint64_t received_bytes_{0};
};

}  // namespace ae::adaptive_datagram_emulation

int main(int argc, char* argv[]) {
  if (argc > 3) {
    std::cerr << "Usage: adaptive-datagram-emulation [csv-path] [seed]\n";
    return 2;
  }
  auto const* csv_path = argc >= 2 ? argv[1] : "adaptive_datagram.csv";
  if (std::string_view{csv_path}.empty()) {
    std::cerr << "CSV path must not be empty.\n";
    return 2;
  }
  std::uint64_t seed = 12345;
  if (argc == 3) {
    std::string_view const text{argv[2]};
    auto const parsed =
        std::from_chars(text.data(), text.data() + text.size(), seed);
    if (text.empty() || parsed.ec != std::errc{} ||
        parsed.ptr != text.data() + text.size()) {
      std::cerr << "Seed must be an unsigned 64-bit decimal integer.\n";
      return 2;
    }
  }
  std::ofstream csv{csv_path, std::ios::out | std::ios::trunc};
  if (!csv.is_open()) {
    std::cerr << "Cannot open CSV output: " << csv_path << '\n';
    return 1;
  }
  ae::adaptive_datagram_emulation::Simulation simulation{seed};
  if (!simulation.Run(csv)) {
    return 1;
  }
  std::cout << "Seed: " << seed << "; CSV: " << csv_path
            << ". Deterministic emulation, not a real-network benchmark.\n";
  return 0;
}
