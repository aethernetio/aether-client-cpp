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

// Desktop counterpart of the product adaptive Wi-Fi probe firmware.
//
// Stays connected over TCP, counts unique probe packets per (session, batch),
// answers PROBE_QUERY with PROBE_RESULT and prints one machine-readable line
// per event. Nothing else is written to stdout: the campaign runner parses it.

#define AE_EXAMPLE_LORA_MODULE 0
#define AE_EXAMPLE_MODEM 0
#ifdef ESP_PLATFORM
#  define AE_EXAMPLE_ESP_WIFI 1
#else
#  define AE_EXAMPLE_ETHERNET 1
#endif

#include <chrono>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <vector>

#include "aether-miscpp/format/format.h"
#include "aether/all.h"
#include "aether/cloud_connections/cloud_server_connection.h"
#include "aether/types/address.h"
#include "probe_protocol.h"
#include "product_probe_select.h"

// IWYU pragma: begin_keeps
#include "../common/aether_construct_esp_wifi.h"
#include "../common/aether_construct_ethernet.h"
#include "../common/aether_construct_lora_module.h"
#include "../common/aether_construct_modem.h"
// IWYU pragma: end_keeps

namespace ae::examples::probe_receiver {
namespace {

using namespace std::chrono_literals;  // NOLINT

// Parent UID of the preprovisioned campaign clients.
constexpr auto kParentUid =
    ae::Uid::FromString("b1ac52c8-8d94-bd39-4c01-a631ac594165");

// Reusing the existing campaign client name keeps the receiver on the UID the
// sensor firmware is preprovisioned to send to.
#ifndef AE_PROBE_RECEIVER_CLIENT_ID
#  define AE_PROBE_RECEIVER_CLIENT_ID "prepared_wifi_cache_rx_v1"
#endif
constexpr char const* kClientId = AE_PROBE_RECEIVER_CLIENT_ID;

#ifndef AE_PROBE_RECEIVER_SERVICE_UID
#  define AE_PROBE_RECEIVER_SERVICE_UID "5aade50f-00d9-4624-b097-e203cdcf1e38"
#endif
constexpr auto kExpectedServiceUid =
    ae::Uid::FromString(AE_PROBE_RECEIVER_SERVICE_UID);

constexpr auto kHealthInterval = 10s;

struct BatchStat {
  std::uint16_t parameter_id{0};
  std::uint8_t stage{0};
  std::uint8_t profile{0};
  std::uint16_t pre_ms{0};
  std::uint16_t post_ms{0};
  std::uint16_t sleep_ms{0};
  probe::BatchSeqTracker tracker{};
};

struct ReceiverState {
  std::vector<std::unique_ptr<ae::P2pStream>> streams;
  std::map<std::uint64_t, BatchStat> batches;
  std::uint32_t probe_data_count{0};
  std::uint32_t hot_data_count{0};
  std::uint32_t query_count{0};
  std::uint32_t summary_count{0};
  std::uint32_t unknown_count{0};
  bool tcp_up{false};
  bool link_down_logged{false};
  std::chrono::steady_clock::time_point last_health{};
};

ReceiverState g_state{};

std::uint64_t BatchKey(std::uint32_t session, std::uint16_t batch_id) {
  return (static_cast<std::uint64_t>(session) << 16) |
         static_cast<std::uint64_t>(batch_id);
}

BatchStat& FindBatch(std::uint32_t session, std::uint16_t batch_id) {
  auto const key = BatchKey(session, batch_id);
  auto it = g_state.batches.find(key);
  if (it == g_state.batches.end()) {
    BatchStat fresh{};
    fresh.tracker.Reset(0);
    it = g_state.batches.emplace(key, fresh).first;
  }
  return it->second;
}

void LogTransport(ae::Client& client) {
  bool any_tcp = false;
  bool any_udp = false;
  int linked = 0;
  for (auto* csc : client.cloud_connection().servers()) {
    if (csc == nullptr) {
      continue;
    }
    auto* cc = csc->client_connection();
    if (cc == nullptr) {
      continue;
    }
    if (cc->stream_info().link_state != ae::LinkState::kLinked) {
      continue;
    }
    ++linked;
    auto ch = cc->server_connection().current_channel();
    if (!ch) {
      continue;
    }
    auto ep = ch->endpoint();
    if (!ep) {
      continue;
    }
    if (ep->protocol == ae::Protocol::kTcp) {
      if (!any_tcp && !g_state.tcp_up) {
        std::cout << "RX_TCP_LINK_UP endpoint=" << ae::Format("{}", *ep)
                  << " server=" << csc->server_id() << "\n";
      }
      any_tcp = true;
    } else if (ep->protocol == ae::Protocol::kUdp) {
      any_udp = true;
    }
  }
  if (any_tcp && !any_udp) {
    if (!g_state.tcp_up) {
      std::cout << "RX_TRANSPORT=TCP\n";
    }
    g_state.tcp_up = true;
    g_state.link_down_logged = false;
  } else if (linked == 0 && client.cloud_connection().count_connections() > 0) {
    if (g_state.tcp_up) {
      std::cout << "RX_TCP_LINK_DOWN\n";
    } else if (!g_state.link_down_logged) {
      std::cout << "RX_TCP_LINK_DOWN waiting_for_tcp\n";
      g_state.link_down_logged = true;
    }
    g_state.tcp_up = false;
  }
  std::cout.flush();
}

void OnProbeData(probe::ProbeData const& msg) {
  ++g_state.probe_data_count;
  auto& batch = FindBatch(msg.session, msg.batch_id);
  batch.parameter_id = msg.parameter_id;
  batch.stage = msg.stage;
  batch.profile = msg.profile;
  batch.pre_ms = msg.pre_ms;
  batch.post_ms = msg.post_ms;
  batch.sleep_ms = msg.sleep_ms;
  batch.tracker.Observe(msg.seq);
}

void OnHotData(probe::HotData const& msg) {
  ++g_state.hot_data_count;
  auto& batch = FindBatch(msg.session, msg.batch_id);
  batch.parameter_id = msg.parameter_id;
  batch.stage = msg.stage;
  batch.profile = msg.profile;
  batch.pre_ms = msg.pre_ms;
  batch.post_ms = msg.post_ms;
  batch.sleep_ms = msg.sleep_ms;
  batch.tracker.Observe(msg.seq);

  // The previous send's timing travels with this packet; the current send's own
  // timing is only known after it completes and its deep sleep is only
  // confirmed by the boot after that.
  // Format takes at most ten replacement fields, so one log line is built from
  // three calls.
  std::cout << ae::Format(
                   "HOT_DATA session={} batch={} stage={} seq={} profile={} "
                   "pre={} post={} sleep={}",
                   msg.session, msg.batch_id, msg.stage, msg.seq, msg.profile,
                   msg.pre_ms, msg.post_ms, msg.sleep_ms)
            << ae::Format(
                   " prev_seq={} prev_flags={} prev_status={} "
                   "prev_connect_us={} prev_cycle_us={} prev_encode_us={} "
                   "prev_sendto_call_us={}",
                   msg.prev_seq, msg.prev_flags, msg.prev_status,
                   msg.prev_connect_us, msg.prev_cycle_us, msg.prev_encode_us,
                   msg.prev_sendto_call_us)
            << ae::Format(
                   " prev_send_to_txdone_us={} prev_txdone_minus_ret_us={} "
                   "prev_actual_post_us={} prev_teardown_us={} "
                   "prev_awake_us={} prev_sleep_us={} prev_wake_overhead_us={}",
                   msg.prev_send_to_txdone_us,
                   msg.prev_txdone_minus_sendto_return_us,
                   msg.prev_actual_post_us, msg.prev_teardown_us,
                   msg.prev_awake_us, msg.prev_sleep_elapsed_us,
                   msg.prev_wake_overhead_us)
            << "\n";
  std::cout.flush();
}

void OnHotSummary(probe::HotSummary const& msg) {
  ++g_state.summary_count;
  std::cout << ae::Format(
                   "HOT_SUMMARY session={} batch={} param={} profile={} "
                   "pre={} post={} sleep={} hot_sent={} hot_fail={}",
                   msg.session, msg.batch_id, msg.parameter_id, msg.profile,
                   msg.pre_ms, msg.post_ms, msg.sleep_ms, msg.hot_sent,
                   msg.hot_fail)
            << ae::Format(" hot_unconfirmed={} reprobe={} invalidations={}",
                          msg.hot_unconfirmed, msg.reprobe_count,
                          msg.batch_invalidations)
            << "\n";
  std::cout.flush();
}

void OnProbeQuery(ae::P2pStream& stream, probe::ProbeQuery const& msg) {
  ++g_state.query_count;
  auto& batch = FindBatch(msg.session, msg.batch_id);
  batch.tracker.set_expected(msg.expected);
  if (msg.parameter_id != 0) {
    batch.parameter_id = msg.parameter_id;
  }

  probe::ProbeResult result{};
  result.session = msg.session;
  result.batch_id = msg.batch_id;
  result.parameter_id = batch.parameter_id;
  result.expected = msg.expected;
  result.unique = batch.tracker.unique();
  result.dup = batch.tracker.dup();
  result.missing = batch.tracker.missing();

  std::cout << ae::Format(
                   "PROBE_RESULT session={} batch={} param={} stage={} "
                   "profile={} pre={} post={} sleep={}",
                   msg.session, msg.batch_id, result.parameter_id,
                   probe::ProbeStageName(
                       static_cast<probe::ProbeStage>(batch.stage)),
                   batch.profile, batch.pre_ms, batch.post_ms, batch.sleep_ms)
            << ae::Format(" expected={} unique={} dup={} missing={} oow={}",
                          result.expected, result.unique, result.dup,
                          result.missing, batch.tracker.out_of_window())
            << "\n";
  std::cout.flush();

  std::uint8_t buffer[probe::kMaxProbeMessageSize]{};
  auto const size = probe::Pack(result, buffer, sizeof(buffer));
  if (size == 0) {
    return;
  }
  stream.Write(ae::DataBuffer{buffer, buffer + size});
}

void OnMessage(ae::P2pStream& stream, ae::DataBuffer const& data) {
  probe::ProbeMsgType type{};
  if (!probe::PeekType(data.data(), data.size(), type)) {
    ++g_state.unknown_count;
    return;
  }
  switch (type) {
    case probe::ProbeMsgType::kProbeData: {
      probe::ProbeData msg{};
      if (probe::Unpack(data.data(), data.size(), msg)) {
        OnProbeData(msg);
      }
      return;
    }
    case probe::ProbeMsgType::kProbeQuery: {
      probe::ProbeQuery msg{};
      if (probe::Unpack(data.data(), data.size(), msg)) {
        OnProbeQuery(stream, msg);
      }
      return;
    }
    case probe::ProbeMsgType::kHotData: {
      probe::HotData msg{};
      if (probe::Unpack(data.data(), data.size(), msg)) {
        OnHotData(msg);
      }
      return;
    }
    case probe::ProbeMsgType::kHotSummary: {
      probe::HotSummary msg{};
      if (probe::Unpack(data.data(), data.size(), msg)) {
        OnHotSummary(msg);
      }
      return;
    }
    case probe::ProbeMsgType::kProbeResult:
      // Receiver-to-device direction only.
      return;
    default:
      ++g_state.unknown_count;
      return;
  }
}

void EmitHealth() {
  auto const now = std::chrono::steady_clock::now();
  if (g_state.last_health != std::chrono::steady_clock::time_point{} &&
      (now - g_state.last_health) < kHealthInterval) {
    return;
  }
  g_state.last_health = now;
  std::cout << ae::Format(
                   "RX_HEALTH tcp_up={} probe_data={} hot_data={} queries={} "
                   "summaries={} batches={} unknown={}",
                   g_state.tcp_up ? 1 : 0, g_state.probe_data_count,
                   g_state.hot_data_count, g_state.query_count,
                   g_state.summary_count,
                   static_cast<std::uint32_t>(g_state.batches.size()),
                   g_state.unknown_count)
            << "\n";
  std::cout.flush();
}

}  // namespace
}  // namespace ae::examples::probe_receiver

int AetherProbeReceiverExample() {
  using namespace ae::examples::probe_receiver;  // NOLINT

  std::cout.setf(std::ios::unitbuf);
  auto aether_app = ae::examples::construct_aether_app();

  ae::Client::ptr client;
  aether_app->aether()
      ->SelectClient(kParentUid, kClientId)
      .result_event()
      .Subscribe([&](ae::Result<ae::Client::ptr, int> const& res) {
        if (!res) {
          std::cout << "RX_ERROR select_client_failed\n";
          aether_app->Exit(1);
          return;
        }
        client = res.value();
        std::cout << ae::Format("RX_READY uid={}", client->uid()) << "\n";
        if (client->uid() != kExpectedServiceUid) {
          // Not fatal: a fresh registration is still usable, but the sensor
          // firmware is preprovisioned for one destination only.
          std::cout << ae::Format("RX_UID_MISMATCH expected={}",
                                  kExpectedServiceUid)
                    << "\n";
        }
        std::cout.flush();

        auto loaded = client.Load();
        loaded->connectivity_policy()->ResetRxTimings();
        loaded->connectivity_policy()
            ->ConfigureRxTimings(ae::RequestPolicy::All{})
            .ForAllPriorities(ae::RxTimingConf::Every(1s).WithWindow(1s));
        LogTransport(*loaded);
        loaded->cloud_connection().servers_update_event().Subscribe(
            [&client]() { LogTransport(*client.Load()); });
        loaded->message_stream_manager().new_port_event().Subscribe(
            [&](ae::P2pPortHandle handle) {
              auto sender = handle.destination();
              auto stream = std::make_unique<ae::P2pStream>(
                  *aether_app, client.Load(), sender, std::move(handle));
              auto* stream_ptr = stream.get();
              stream_ptr->out_data_event().Subscribe(
                  [stream_ptr](ae::DataBuffer const& data) {
                    OnMessage(*stream_ptr, data);
                  });
              g_state.streams.push_back(std::move(stream));
            });
      });

  while (!aether_app->IsExited()) {
    auto const next_time = aether_app->Update(ae::Now());
    if (client) {
      LogTransport(*client.Load());
      EmitHealth();
    }
    aether_app->WaitUntil(next_time);
  }
  return aether_app->ExitCode();
}
