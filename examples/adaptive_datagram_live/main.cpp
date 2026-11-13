#include "aether/adaptive_datagram/payload_size_controller.h"
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
#include <array>
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

void Pump(AetherApp& app, bool wait = true) {
  auto const wake = app.Update(Now());
  auto const now = Now();
  if (wait && wake > now) {
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

int Run(char const* output, unsigned seconds, adaptive_datagram::Config config,
        unsigned requested_payload, unsigned growth_ceiling,
        unsigned local_queue_limit) {
#if !AE_SUPPORT_UDP || AE_SUPPORT_TCP || !AE_FILTRATION
  std::cerr << "Use a UDP-only filtration build and TCP-bootstrapped state.\n";
  return 4;
#else
  if (!stream_matrix_bench::RegistrationSeedReadyForUdpBench(
          std::cerr, std::filesystem::current_path())) {
    return 3;
  }
  std::size_t kPayload = requested_payload;
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
  std::vector<std::array<std::uint64_t, 5> > local_write_samples;
  local_write_samples.reserve(1000000);
  config.on_local_write_complete =
      [&](std::uint64_t message_sequence, Duration queue_wait,
          Duration lower_write_wait, bool success) {
        local_write_samples.push_back(
            {message_sequence, static_cast<std::uint64_t>(queue_wait.count()),
             static_cast<std::uint64_t>(lower_write_wait.count()), Micros(),
             success ? 1U : 0U});
      };
  auto peer_config = config;
  peer_config.on_local_write_complete = {};
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
          60)) {
    std::cerr << "UDP P2P link unavailable or payload exceeds its budget.\n";
    return 5;
  }
  auto const payload_budget = std::min(sender.stream_info().max_element_size,
                                       receiver.stream_info().max_element_size);
  if (kPayload == 0) {
    kPayload = payload_budget;
  }
  if (kPayload < 32 || kPayload > payload_budget) {
    return 5;
  }

  adaptive_datagram::PayloadSizeController size_controller{
      kPayload, std::min<std::size_t>(payload_budget, growth_ceiling)};
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
  std::vector<std::array<std::uint64_t, 5> > samples;
  samples.reserve(1000000);
  std::vector<std::array<std::uint64_t, 4> > offered_samples;
  offered_samples.reserve(1000000);
  std::uint64_t duplicates = 0, corruptions = 0;
  std::vector<std::array<std::uint64_t, 10> > controller_trace;
  std::uint64_t generation_ns = 0, pump_ns = 0, timing_samples = 0;
  std::uint64_t loop_count = 0;
  auto next_trace = Steady::now();
  std::uint64_t callback_deadline_violations = 0;
  std::uint64_t max_queued = 0, max_flight = 0;
  auto const observation_start = Micros();
  std::uint64_t measured_offered = 0;
  auto on_datagram = [&rx, &config, &samples, &duplicates, &corruptions,
                      &callback_deadline_violations, observation_start, run,
                      payload_budget](DataBuffer const& data) {
    if (data.size() < 32 || data.size() > payload_budget ||
        (Get(data, 24) >> 1) != data.size() || Get(data, 0) != run ||
        Get(data, 16) > Micros()) {
      ++corruptions;
      rx.sane = false;
      return;
    }
    auto const sequence = Get(data, 8);
    if (!rx.seen.insert(sequence).second) {
      ++duplicates;
      rx.sane = false;
      return;
    }
    for (std::size_t i = 32; i < data.size(); ++i) {
      if (data[i] != static_cast<std::uint8_t>(sequence + i)) {
        ++corruptions;
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
    if (latency >
        static_cast<std::uint64_t>(config.latency_target.count()) + 1000) {
      ++callback_deadline_violations;
    }
    samples.push_back({Get(data, 8), latency, Micros() - observation_start,
                       (Get(data, 24) & 1), data.size()});
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
    if ((Get(data, 24) & 1) == 1) {
      rx.measured_received += data.size();
    }
  };
  Subscription incoming = receiver.out_data_event().Subscribe(
      [&on_datagram](DataBuffer const& data) { on_datagram(data); });

  csv << "second,phase,offered_Bps,goodput_Bps,rate_Bps,window_loss_per_mille,"
         "window_valid,latency_p50_us,latency_p95_us,latency_p99_us,"
         "abs_ipdv_p95_us,jitter_ewma_us,accepted,sent,delivered,lost,late,"
         "expired,unconfirmed,local_failures,queued,in_flight,feedback_stale,"
         "feedback_incomplete,udp_only,constraints_unmet,path_latency_us,"
         "base_latency_us,rate_ceiling_Bps,history_limited,in_flight_capacity,"
         "payload_bytes,payload_budget,history_capacity,elapsed_us,interval_us,"
         "latency_max_us,rtt_us,window_max_latency_us,window_samples,delivery_"
         "rate_Bps,"
         "wire_bytes,rejected,peer_duplicates,peer_malformed,max_queued,max_in_"
         "flight,"
         "callback_deadline_violations,corruptions\n";
  std::uint64_t sequence = 0;
  std::uint64_t offered = 0;
  std::uint64_t previous_offered = 0;
  std::uint64_t previous_received = 0;
  unsigned row = 0;
  std::uint64_t previous_row_us = 0;
  auto const start = Steady::now();
  auto const measure_start = start + std::chrono::seconds{3};
  auto const measure_end = measure_start + std::chrono::seconds{seconds};
  auto const end = measure_end + config.latency_target +
                   config.feedback_timeout + std::chrono::seconds{1};
  while (!app->IsExited() && Steady::now() < end) {
    auto const now = Steady::now();
    auto const info = sender.stream_info();
    auto const path = sender.InspectSendPathTransport();
    bool const udp_only = path.linked_udp_endpoint && !path.linked_tcp_endpoint;
    if (!udp_only || info.link_state != LinkState::kLinked) {
      std::cerr << "UDP route was lost during measurement.\n";
      return 5;
    }
    // Fill a bounded producer queue before driving the scheduler. This changes
    // admission depth, not the single active lower-write policy or ACK credit.
    bool const time_this_loop = (++loop_count % 64) == 0;
    auto const generation_start =
        time_this_loop ? Steady::now() : Steady::time_point{};
    while (Steady::now() < measure_end && sender.stream_info().is_writable &&
           sender.statistics().queued < local_queue_limit) {
      auto const generation_now = Steady::now();
      if (generation_now >= measure_end) break;
      bool const measuring = generation_now >= measure_start;
      DataBuffer data(kPayload, 0);
      Put(data, 0, run);
      Put(data, 8, ++sequence);
      auto const prepared_at = Micros();
      Put(data, 16, prepared_at);
      Put(data, 24,
          (static_cast<std::uint64_t>(data.size()) << 1) | (measuring ? 1 : 0));
      for (std::size_t i = 32; i < data.size(); ++i) {
        data[i] = static_cast<std::uint8_t>(sequence + i);
      }
      sender.Write(std::move(data));
      offered_samples.push_back(
          {sequence, prepared_at - observation_start, measuring, kPayload});
      offered += kPayload;
      if (measuring) {
        measured_offered += kPayload;
      }
    }
    // Continuous offered load must not be capped by a host timer's sleep
    // granularity. The SDK pacer still owns transmission and the application
    // queue stays bounded to one datagram. Registration and drain can sleep.
    auto const pump_start =
        time_this_loop ? Steady::now() : Steady::time_point{};
    Pump(*app, now >= measure_end);
    if (time_this_loop) {
      auto const pump_end = Steady::now();
      generation_ns += static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(pump_start -
                                                               generation_start)
              .count());
      pump_ns += static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(pump_end -
                                                               pump_start)
              .count());
      ++timing_samples;
    }
    if (now >= next_trace) {
      auto const& metrics = sender.metrics();
      auto const st = sender.statistics();
      controller_trace.push_back(
          {Micros() - observation_start, metrics.latest_latency.count(),
           metrics.rtt.count(), sender.rate_bytes_per_second(),
           metrics.window_loss_per_mille, metrics.window_valid,
           metrics.constraints_unmet, metrics.history_limited, st.in_flight,
           st.queued});
      next_trace = now + std::chrono::milliseconds{100};
    }
    auto const live_stats = sender.statistics();
    max_queued = std::max<std::uint64_t>(max_queued, live_stats.queued);
    max_flight = std::max<std::uint64_t>(max_flight, live_stats.in_flight);
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
          << ',' << m.history_limited << ',' << config.max_in_flight_packets
          << ',' << kPayload << ',' << payload_budget << ','
          << config.max_history_packets;
      auto const elapsed_us = static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::microseconds>(Steady::now() -
                                                                start)
              .count());
      auto const peer_stats = receiver.statistics();
      csv << ',' << elapsed_us << ',' << elapsed_us - previous_row_us << ','
          << (rx.latencies.empty()
                  ? 0
                  : *std::max_element(rx.latencies.begin(), rx.latencies.end()))
          << ',' << m.rtt.count() << ',' << m.window_max_latency.count() << ','
          << m.window_samples << ',' << m.delivery_rate << ','
          << stats.wire_bytes << ',' << stats.rejected << ','
          << peer_stats.duplicates << ',' << peer_stats.malformed << ','
          << max_queued << ',' << max_flight << ','
          << callback_deadline_violations << ',' << corruptions << '\n';
      previous_row_us = elapsed_us;
      csv.flush();
      if (growth_ceiling && now >= measure_start && now < measure_end) {
        bool const overloaded =
            (m.window_valid &&
             m.window_loss_per_mille > config.max_loss_per_mille) ||
            m.window_max_latency > config.latency_target;
        bool const healthy = m.window_valid && !m.feedback_stale &&
                             !m.feedback_incomplete && !m.history_limited &&
                             !m.constraints_unmet && !overloaded;
        kPayload = size_controller.Observe(
            healthy, overloaded, rx.received_bytes - previous_received);
      }
      previous_offered = offered;
      previous_received = rx.received_bytes;
      rx.latencies.clear();
      rx.ipdv.clear();
    }
  }
  std::ofstream sample_output{std::string{output} + ".latency.csv"};
  sample_output << "sequence,latency_us,received_elapsed_us,measurement_packet,"
                   "payload_bytes\n";
  for (auto const& sample : samples) {
    sample_output << sample[0] << ',' << sample[1] << ',' << sample[2] << ','
                  << sample[3] << ',' << sample[4] << '\n';
  }
  std::ofstream offered_output{std::string{output} + ".offered.csv"};
  offered_output
      << "sequence,prepared_elapsed_us,measurement_packet,payload_bytes\n";
  for (auto const& sample : offered_samples) {
    offered_output << sample[0] << ',' << sample[1] << ',' << sample[2] << ','
                   << sample[3] << '\n';
  }
  std::ofstream local_output{std::string{output} + ".local-write.csv"};
  local_output << "sequence,queue_wait_us,lower_write_wait_us,completed_epoch_"
                  "us,success\n";
  for (auto const& sample : local_write_samples) {
    local_output << sample[0] << ',' << sample[1] << ',' << sample[2] << ','
                 << sample[3] << ',' << sample[4] << '\n';
  }
  std::ofstream trace_output{std::string{output} + ".controller.csv"};
  trace_output
      << "elapsed_us,path_latency_us,rtt_us,rate_Bps,window_loss_per_mille,"
         "window_valid,constraints_unmet,history_limited,in_flight,queued\n";
  for (auto const& trace : controller_trace) {
    for (std::size_t i = 0; i < trace.size(); ++i) {
      if (i) trace_output << ',';
      trace_output << trace[i];
    }
    trace_output << '\n';
  }
  auto const stats = sender.statistics();
  auto const feedback_stats = receiver.statistics();
  rx.sane = rx.sane && stats.delivered > 0 &&
            feedback_stats.feedback_sent > 0 && stats.accepted == sequence &&
            stats.rejected == 0 &&
            stats.accepted == stats.delivered + stats.lost + stats.late +
                                  stats.expired + stats.unconfirmed +
                                  stats.local_failures + stats.cancelled &&
            stats.queued == 0 && stats.in_flight == 0 && csv.good() &&
            local_output.good();
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
            << " observation_start_epoch_us=" << observation_start
            << " generation_ns_sampled=" << generation_ns
            << " pump_ns_sampled=" << pump_ns
            << " timing_samples=" << timing_samples
            << " duplicates=" << duplicates << " corruptions=" << corruptions
            << " peer_duplicates=" << feedback_stats.duplicates
            << " peer_malformed=" << feedback_stats.malformed
            << " callback_deadline_violations=" << callback_deadline_violations
            << " peer_feedback_sent=" << feedback_stats.feedback_sent << '\n';
  return rx.sane ? 0 : 1;
#endif
}

}  // namespace ae::adaptive_datagram_live

int main(int argc, char* argv[]) {
  // Deliberately small positional interface, with strict validation.
  unsigned values[]{10, 50, 1000, 100, 4000000, 800, 256, 0, 1};
  if (argc > 11) {
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
  if (values[0] == 0 || values[0] > 600 || values[1] > 1000 || values[2] == 0 ||
      values[2] > 60000 || values[3] == 0 || values[3] > 5000 ||
      values[4] == 0 || values[4] > 1000000000 || values[5] > 65535 ||
      (values[5] != 0 && values[5] < 32) || values[6] == 0 || values[6] > 512 ||
      values[7] > 65000 || values[8] == 0 || values[8] > 128 ||
      (values[7] && (!values[5] || values[7] < values[5]))) {
    std::cerr << "Usage: adaptive-datagram-live [csv] [seconds:1..600] "
                 "[loss_per_mille] [window_ms:1..60000] [latency_ms:1..5000] "
                 "[rate_ceiling_Bps:1..1000000000] [payload_bytes:0=budget] "
                 "[in_flight] [growth_ceiling:0=off] [local_queue:1..128]\n";
    return 2;
  }
  ae::adaptive_datagram::Config config;
  config.max_in_flight_packets = values[6];
  config.max_loss_per_mille = values[1];
  config.loss_window = std::chrono::milliseconds{values[2]};
  config.latency_target = std::chrono::milliseconds{values[3]};
  config.max_rate_bytes_per_second = values[4];
  config.max_datagram_size = std::max<std::size_t>(
      config.max_datagram_size, std::max(values[5], values[7]) + 96);
  if (argc >= 7) {
    // Provision measurement history for the requested rate. Keep the native
    // in-flight window: the protocol's receipt bitmap supports 512 packets.
    // Expose that limit in CSV instead of accepting an invalid wire policy.
    auto const packets_per_second =
        (config.max_rate_bytes_per_second + (values[5] ? values[5] : 256) - 1) /
        (values[5] ? values[5] : 256);
    config.max_history_packets =
        static_cast<std::size_t>(std::max<std::uint64_t>(
            config.max_history_packets,
            std::min<std::uint64_t>(
                1U << 20U, (packets_per_second * values[2] * 2 + 999) / 1000)));
  }
  if (!config.Validate()) {
    return 2;
  }
  return ae::adaptive_datagram_live::Run(
      argc >= 2 ? argv[1] : "adaptive-live.csv", values[0], config, values[5],
      values[7], values[8]);
}
