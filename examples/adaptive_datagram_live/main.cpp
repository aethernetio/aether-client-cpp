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

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string_view>
#include <unordered_set>
#include <vector>

#define AE_EXAMPLE_ETHERNET 1
#include "../benches/stream_matrix/bench_persistence.h"
#include "../common/aether_construct_ethernet.h"

namespace ae::adaptive_datagram_live {
using Steady = std::chrono::steady_clock;

std::uint64_t Micros() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          Now().time_since_epoch())
          .count());
}

void Put(DataBuffer& data, std::size_t offset, std::uint64_t value) {
  for (unsigned i = 0; i < 8; ++i) {
    data[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
  }
}
std::uint64_t Get(DataBuffer const& data, std::size_t offset) {
  std::uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i) {
    value |= std::uint64_t{data[offset + i]} << (8 * i);
  }
  return value;
}
std::uint64_t Percentile(std::vector<std::uint64_t> values, unsigned p) {
  if (values.empty()) {
    return 0;
  }
  auto const index = (values.size() * p + 99) / 100 - 1;
  std::nth_element(values.begin(), values.begin() + index, values.end());
  return values[index];
}

void Pump(AetherApp& app) {
  auto const wake = app.Update(Now());
  auto const now = Now();
  if (wake > now) {
    app.WaitUntil(std::min(wake, now + std::chrono::milliseconds{1}));
  }
}
bool Until(AetherApp& app, auto predicate, unsigned seconds) {
  auto const deadline = Steady::now() + std::chrono::seconds{seconds};
  while (!app.IsExited() && Steady::now() < deadline) {
    if (predicate()) {
      return true;
    }
    Pump(app);
  }
  return predicate();
}

int Run(char const* output, unsigned seconds,
        adaptive_datagram::Config config) {
#if !AE_SUPPORT_UDP || AE_SUPPORT_TCP || !AE_FILTRATION
  std::cerr << "Use a UDP-only filtration build and TCP-bootstrapped state.\n";
  return 4;
#else
  if (!stream_matrix_bench::RegistrationSeedReadyForUdpBench(
          std::cerr, std::filesystem::current_path())) {
    return 3;
  }
  constexpr std::size_t kPayload = 800;
  std::ofstream csv{output, std::ios::trunc};
  if (!csv || !config.Validate()) {
    return 2;
  }
  auto app = examples::construct_aether_app();
  Client::ptr a;
  Client::ptr b;
  bool acquire_failed = false;
  auto const parent = Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4");
  MultiSubscription acquire;
  acquire += app->aether()
                 ->SelectClient(parent, "sender")
                 .result_event()
                 .Subscribe([&](auto const& result) {
                   if (result) {
                     a = result.value();
                   } else {
                     acquire_failed = true;
                   }
                 });
  acquire += app->aether()
                 ->SelectClient(parent, "receiver")
                 .result_event()
                 .Subscribe([&](auto const& result) {
                   if (result) {
                     b = result.value();
                   } else {
                     acquire_failed = true;
                   }
                 });
  if (!Until(
          *app, [&] { return (a && b) || acquire_failed; }, 30) ||
      !a || !b) {
    std::cerr << "Cannot load sender/receiver; run TCP bootstrap first.\n";
    return 3;
  }
  auto const context = AeContext{*app};
  auto client_a = a.Load();
  auto client_b = b.Load();
  client_a->connectivity_policy()
      ->ConfigureRxTimings(RequestPolicy::All{})
      .ForPriority<0>(RxTimingConf::Every(std::chrono::seconds{1}));
  client_b->connectivity_policy()
      ->ConfigureRxTimings(RequestPolicy::All{})
      .ForPriority<0>(RxTimingConf::Every(std::chrono::seconds{1}));
  auto const run = std::max<std::uint64_t>(1, Micros());
  config.session_id = run;
  config.peer_session_id = run + 1;
  auto peer_config = config;
  peer_config.session_id = run + 1;
  peer_config.peer_session_id = run;
  P2pAdaptiveDatagramStream sender{
      context, client_a, client_b->uid(),
      client_a->message_stream_manager().CreatePort(client_b->uid()), config};
  P2pAdaptiveDatagramStream receiver{
      context, client_b, client_a->uid(),
      client_b->message_stream_manager().CreatePort(client_a->uid()),
      peer_config};
  if (!Until(
          *app,
          [&] {
            return sender.stream_info().link_state == LinkState::kLinked &&
                   receiver.stream_info().link_state == LinkState::kLinked;
          },
          60) ||
      sender.stream_info().max_element_size < kPayload) {
    std::cerr << "UDP P2P link unavailable or payload exceeds its budget.\n";
    return 5;
  }

  struct ReceiveState {
    bool sane = true;
    std::unordered_set<std::uint64_t> seen;
    std::vector<std::uint64_t> latencies;
    std::vector<std::uint64_t> ipdv;
    std::optional<std::uint64_t> previous_latency;
    double jitter = 0;
    std::uint64_t received_bytes = 0;
    std::uint64_t measured_received = 0;
  } rx;
  std::uint64_t measured_offered = 0;
  Subscription incoming = receiver.out_data_event().Subscribe(
      [&rx, &config, run](DataBuffer const& data) {
        if (data.size() != kPayload || Get(data, 0) != run ||
            Get(data, 16) > Micros() || !rx.seen.insert(Get(data, 8)).second) {
          rx.sane = false;
          return;
        }
        auto const sequence = Get(data, 8);
        for (std::size_t i = 32; i < data.size(); ++i) {
          if (data[i] != static_cast<std::uint8_t>(sequence + i)) {
            rx.sane = false;
            return;
          }
        }
        auto const latency = Micros() - Get(data, 16);
        // Payload timestamp precedes Write() admission; allow 1 ms for harness
        // serialization and the receive callback after the protocol's check.
        rx.sane = rx.sane && latency <= static_cast<std::uint64_t>(
                                            config.latency_target.count()) +
                                            1000;
        rx.latencies.push_back(latency);
        if (rx.previous_latency) {
          auto const variation = latency > *rx.previous_latency
                                     ? latency - *rx.previous_latency
                                     : *rx.previous_latency - latency;
          rx.ipdv.push_back(variation);
          rx.jitter += (static_cast<double>(variation) - rx.jitter) / 16.0;
        }
        rx.previous_latency = latency;
        rx.received_bytes += data.size();
        if (Get(data, 24) == 1) {
          rx.measured_received += data.size();
        }
      });

  csv << "second,phase,offered_Bps,goodput_Bps,rate_Bps,window_loss_per_mille,"
         "window_valid,latency_p50_us,latency_p95_us,latency_p99_us,"
         "abs_ipdv_p95_us,jitter_ewma_us,accepted,sent,delivered,lost,late,"
         "expired,unconfirmed,local_failures,queued,in_flight,feedback_stale,"
         "feedback_incomplete,udp_only,constraints_unmet,path_latency_us,"
         "base_latency_us,rate_ceiling_Bps,history_limited\n";
  std::uint64_t sequence = 0;
  std::uint64_t offered = 0;
  std::uint64_t previous_offered = 0;
  std::uint64_t previous_received = 0;
  unsigned row = 0;
  auto const start = Steady::now();
  auto const measure_start = start + std::chrono::seconds{3};
  auto const measure_end = measure_start + std::chrono::seconds{seconds};
  auto const end = measure_end + config.latency_target +
                   config.feedback_timeout + std::chrono::seconds{1};
  while (!app->IsExited() && Steady::now() < end) {
    auto const now = Steady::now();
    bool const measuring = now >= measure_start && now < measure_end;
    auto const info = sender.stream_info();
    auto const path = sender.InspectSendPathTransport();
    bool const udp_only = path.linked_udp_endpoint && !path.linked_tcp_endpoint;
    if (!udp_only || info.link_state != LinkState::kLinked) {
      std::cerr << "UDP route was lost during measurement.\n";
      return 5;
    }
    // Keep at most one application datagram waiting for the pacer. This offers
    // continuous demand without hiding congestion in an unbounded producer
    // queue.
    if (now < measure_end && info.is_writable &&
        sender.statistics().queued == 0) {
      DataBuffer data(kPayload, 0);
      Put(data, 0, run);
      Put(data, 8, ++sequence);
      Put(data, 16, Micros());
      Put(data, 24, measuring ? 1 : 0);
      for (std::size_t i = 32; i < data.size(); ++i) {
        data[i] = static_cast<std::uint8_t>(sequence + i);
      }
      sender.Write(std::move(data));
      offered += kPayload;
      if (measuring) {
        measured_offered += kPayload;
      }
    }
    Pump(*app);
    auto const elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(Steady::now() - start)
            .count();
    if (elapsed > row) {
      ++row;
      auto const stats = sender.statistics();
      auto const& m = sender.metrics();
      csv << row << ','
          << (now < measure_start ? "warmup"
              : now < measure_end ? "measure"
                                  : "drain")
          << ',' << offered - previous_offered << ','
          << rx.received_bytes - previous_received << ','
          << sender.rate_bytes_per_second() << ',' << m.window_loss_per_mille
          << ',' << m.window_valid << ',' << Percentile(rx.latencies, 50) << ','
          << Percentile(rx.latencies, 95) << ',' << Percentile(rx.latencies, 99)
          << ',' << Percentile(rx.ipdv, 95) << ',' << rx.jitter << ','
          << stats.accepted << ',' << stats.sent << ',' << stats.delivered
          << ',' << stats.lost << ',' << stats.late << ',' << stats.expired
          << ',' << stats.unconfirmed << ',' << stats.local_failures << ','
          << stats.queued << ',' << stats.in_flight << ',' << m.feedback_stale
          << ',' << m.feedback_incomplete << ',' << udp_only << ','
          << m.constraints_unmet << ',' << m.latest_latency.count() << ','
          << m.base_latency.count() << ',' << config.max_rate_bytes_per_second
          << ',' << m.history_limited << '\n';
      csv.flush();
      previous_offered = offered;
      previous_received = rx.received_bytes;
      rx.latencies.clear();
      rx.ipdv.clear();
    }
  }
  auto const stats = sender.statistics();
  auto const feedback_stats = receiver.statistics();
  rx.sane = rx.sane && stats.delivered > 0 &&
            feedback_stats.feedback_sent > 0 && stats.accepted == sequence &&
            stats.rejected == 0 &&
            stats.accepted == stats.delivered + stats.lost + stats.late +
                                  stats.expired + stats.unconfirmed +
                                  stats.local_failures + stats.cancelled &&
            stats.queued == 0 && stats.in_flight == 0 && csv.good();
  std::cout << "adaptive_live status=" << (rx.sane ? "PASS" : "FAIL")
            << " transport=UDP clock=shared_process"
            << " measured_goodput_Bps=" << rx.measured_received / seconds
            << " measured_offered_bytes=" << measured_offered
            << " measured_received_bytes=" << rx.measured_received
            << " accepted=" << stats.accepted << " sent=" << stats.sent
            << " delivered=" << stats.delivered << " lost=" << stats.lost
            << " late=" << stats.late << " expired=" << stats.expired
            << " unconfirmed=" << stats.unconfirmed
            << " local_failures=" << stats.local_failures
            << " peer_feedback_sent=" << feedback_stats.feedback_sent << '\n';
  return rx.sane ? 0 : 1;
#endif
}

}  // namespace ae::adaptive_datagram_live

int main(int argc, char* argv[]) {
  // Deliberately small positional interface, with strict validation.
  unsigned values[]{10, 50, 1000, 100, 4000000};
  if (argc > 7) {
    return 2;
  }
  for (int i = 2; i < argc; ++i) {
    std::string_view const text{argv[i]};
    auto const result =
        std::from_chars(text.data(), text.data() + text.size(), values[i - 2]);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {
      return 2;
    }
  }
  if (values[0] == 0 || values[0] > 120 || values[1] > 1000 || values[2] == 0 ||
      values[2] > 60000 || values[3] == 0 || values[3] > 5000 ||
      values[4] == 0 || values[4] > 1000000000) {
    std::cerr << "Usage: adaptive-datagram-live [csv] [seconds:1..120] "
                 "[loss_per_mille] [window_ms:1..60000] [latency_ms:1..5000] "
                 "[rate_ceiling_Bps:1..1000000000]\n";
    return 2;
  }
  ae::adaptive_datagram::Config config;
  config.max_loss_per_mille = values[1];
  config.loss_window = std::chrono::milliseconds{values[2]};
  config.latency_target = std::chrono::milliseconds{values[3]};
  config.max_rate_bytes_per_second = values[4];
  if (argc == 7) {
    // Provision the measurement history and in-flight budget for the requested
    // ceiling, rather than measuring the default fixed-memory cap. Validate()
    // still rejects resource requests exceeding the stream's supported bounds.
    auto const packets_per_second =
        (config.max_rate_bytes_per_second + 799) / 800;
    config.max_history_packets = static_cast<std::size_t>(std::max<std::uint64_t>(
        config.max_history_packets,
        (packets_per_second * values[2] * 2 + 999) / 1000));
    config.max_in_flight_packets = static_cast<std::size_t>(std::max<std::uint64_t>(
        config.max_in_flight_packets,
        (packets_per_second * (values[3] + 500) + 999) / 1000));
  }
  if (!config.Validate()) { return 2; }
  return ae::adaptive_datagram_live::Run(
      argc >= 2 ? argv[1] : "adaptive-live.csv", values[0], config);
}
