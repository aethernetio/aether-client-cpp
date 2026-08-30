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

#include "client_role.h"

#include <chrono>
#include <cstring>
#include <limits>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#include <windows.h>
// Windows.h maps RegisterClass -> RegisterClassA/W; aether's Registry uses
// RegisterClass by name.
#if defined(RegisterClass)
#  undef RegisterClass
#endif

#define AE_EXAMPLE_ETHERNET 1
#include "aether/all.h"
#include "aether/ae_actions/query_peer_presence.h"
#include "aether/ae_actions/query_peer_receive_schedule.h"
#include "aether/ae_actions/announce_next_ping_unknown.h"
#include "aether/channels/channel.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/cloud_connections/ping_schedule_guard.h"
#include "aether/cloud_connections/ping_cloud_servers.h"
#include "aether/ae_actions/ping_test_faults.h"
#include "aether/receive_schedule.h"
#include "aether/server_connections/server_connection.h"

#include "common/bench_ipc.h"
#include "common/bench_message.h"
#include "common/directory_domain_storage.h"
#include "common/udp_proof.h"

namespace ae::test_uap_ping_retry_window {

#if AE_ENABLE_PING_TEST_FAULTS
using ae::PingFaultMode;
using ae::PingFaultPlan;
using ae::PingTestFaults;
#endif
using ae::ServerId;
using ae::bench::uap::BenchProtocol;
using ae::bench::uap::ChannelProof;
using ae::bench::uap::CollectDestinationProofFromCloud;
using ae::bench::uap::CollectOwnCloudProof;
using ae::bench::uap::DeliveryBenchMessage;
using ae::bench::uap::DeserializeDeliveryBenchMessage;
using ae::bench::uap::DirectoryDomainStorage;
using ae::bench::uap::EventKind;
using ae::bench::uap::HashRunId;
using ae::bench::uap::IpcFrame;
using ae::bench::uap::IpcType;
using ae::bench::uap::NamedPipeClient;
using ae::bench::uap::PackUdpProofFrame;
using ae::bench::uap::SerializeDeliveryBenchMessage;
using ae::bench::uap::UdpProofPath;
using IpcSide = ae::bench::uap::Side;
namespace {
constexpr std::uint8_t kIpcArmFault = 13;
constexpr std::uint8_t kIpcSendTagged = 14;
constexpr std::uint8_t kIpcQueryNow = 15;
constexpr std::uint8_t kIpcPingTraceEx = 16;
constexpr std::uint8_t kIpcAnnounceUnknown = 17;
constexpr std::uint8_t kIpcScheduleState = 18;
constexpr std::uint8_t kIpcPingBudget = 19;
constexpr std::uint8_t kIpcQueryStats = 20;
constexpr std::uint8_t kIpcFaultTrace = 21;
constexpr std::uint32_t kTagRequestLossQueued = 1;
constexpr std::uint32_t kTagResponseLossFirstWindow = 2;
constexpr std::uint32_t kTagAfterRetryWindow = 3;


constexpr auto kBobPingInterval = std::chrono::milliseconds{3000};
constexpr auto kBobReceiveWindow = std::chrono::milliseconds{1000};
constexpr auto kSkipIfCloserThan = std::chrono::milliseconds{50};
constexpr std::size_t kWarmupSamples = 10;

inline std::int64_t TimePointUs(TimePoint tp) {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             tp.time_since_epoch())
      .count();
}

// Duration is unsigned; never add a negative chrono duration to TimePoint.
inline TimePoint AddOffsetMs(TimePoint base, std::int64_t offset_ms) {
  auto const mag_ms = offset_ms < 0 ? -offset_ms : offset_ms;
  auto const mag = std::chrono::duration_cast<Duration>(
      std::chrono::milliseconds{mag_ms});
  if (offset_ms >= 0) {
    return base + mag;
  }
  return base - mag;
}

inline std::int64_t DurationUs(Duration d) {
  return static_cast<std::int64_t>(d.count());
}

inline std::int64_t BenchProtocolFromAe(Protocol protocol) {
  if (protocol == Protocol::kUdp) {
    return static_cast<std::int64_t>(BenchProtocol::kUdp);
  }
  if (protocol == Protocol::kTcp) {
    return static_cast<std::int64_t>(BenchProtocol::kTcp);
  }
  return static_cast<std::int64_t>(BenchProtocol::kUnknown);
}

inline std::int64_t SteadyUsNow() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

inline std::uint64_t QpcNow() {
  LARGE_INTEGER v{};
  QueryPerformanceCounter(&v);
  return static_cast<std::uint64_t>(v.QuadPart);
}

#if AE_ENABLE_PING
struct PendingPingTrace {
  PingTraceEvent event;
  std::int64_t steady_us{0};
  std::int64_t qpc{0};
};
std::vector<PendingPingTrace> g_pending_ping_traces;
std::vector<PendingPingTrace> g_all_ping_traces;

void OnPingTrace(PingTraceEvent const& event) {
  PendingPingTrace rec{event, SteadyUsNow(), static_cast<std::int64_t>(QpcNow())};
  g_pending_ping_traces.push_back(rec);
  if (g_all_ping_traces.size() < 4096) {
    g_all_ping_traces.push_back(rec);
  }
}

#if AE_ENABLE_PING_TEST_FAULTS
struct PendingFaultTrace {
  PingFaultTraceEvent event;
  std::int64_t steady_us{0};
};
std::vector<PendingFaultTrace> g_pending_fault_traces;

void OnPingFaultTrace(PingFaultTraceEvent const& event) {
  g_pending_fault_traces.push_back(
      PendingFaultTrace{event, SteadyUsNow()});
}
#endif
#endif

inline void UidToHalves(Uid const& uid, std::int64_t& lo, std::int64_t& hi) {
  std::memcpy(&lo, uid.value.data(), 8);
  std::memcpy(&hi, uid.value.data() + 8, 8);
}

inline Uid UidFromHalves(std::int64_t lo, std::int64_t hi) {
  Uid uid{};
  std::memcpy(uid.value.data(), &lo, 8);
  std::memcpy(uid.value.data() + 8, &hi, 8);
  return uid;
}

struct RoleState {
  IpcSide side{};
  std::int64_t ping_interval_ms{3000};
  std::int64_t receive_window_ms{1000};
  std::uint32_t run_id_hash{0};
  NamedPipeClient pipe;
  std::unique_ptr<AetherApp> app;
  Client::ptr client;
  Uid peer_uid{};
  bool peer_set{false};
  std::shared_ptr<P2pStream> stream;
  std::vector<std::shared_ptr<P2pStream>> retired_streams_;
  Subscription stream_sub;
  Subscription new_port_sub;
  Subscription select_sub;
  Subscription announce_sub;
  Subscription query_sub;
  bool query_state_only_{false};
  bool query_in_flight_{false};
  std::int64_t pending_query_checkpoint_{-1};
  std::int64_t query_attempts_{0};
  std::int64_t query_created_{0};
  std::int64_t query_reused_{0};
  std::int64_t query_skipped_inflight_{0};
  std::int64_t query_extra_subscribers_{0};
  Subscription extra_query_sub_;
  PeerTimingQueryCoverage last_coverage_{};
  Subscription dest_cloud_sub;
  bool dest_proof_sent{false};
  bool dest_cloud_failed_{false};
  bool own_proof_sent{false};
  std::optional<TimePoint> dest_retry_at_{};
  ChannelProof own_proof{};
  ChannelProof dest_proof{};
  std::unordered_map<std::uint32_t, int> seen;
  std::uint32_t ipc_seq{0};
  bool exit_requested{false};
  bool client_ready{false};
  bool warmup_active{false};
  std::optional<TimePoint> warmup_requery_at_{};
  bool sample_in_flight{false};
  std::uint32_t pending_sequence{0};
  std::uint32_t pending_offset_ms{0};
  std::int64_t pending_offset_signed_{0};
  std::optional<TimePoint> send_at_{};
  std::optional<TimePoint> requery_at_{};
  std::int64_t pending_last_us_{0};
  std::int64_t pending_next_us_{-1};
  std::vector<ServerTimingDiagnostic> last_diagnostics_{};
  std::int64_t pending_schedule_server_id_{0};
  std::int64_t pending_route_generation_{0};
  std::int64_t pending_protocol_{0};
  std::int64_t pending_raw_delta_ms_{0};
  std::int64_t pending_last_connect_ms_{0};
  std::int64_t pending_qsend_us_{0};
  std::int64_t pending_one_way_us_{0};
  std::int64_t pending_target_us_{0};

  bool Emit(IpcType type, EventKind kind = EventKind::kAck,
            std::uint32_t sequence = 0, std::uint32_t offset_ms = 0,
            std::int64_t a = 0, std::int64_t b = 0, std::int64_t c = 0,
            std::int64_t d = 0, std::int64_t e = 0, std::int64_t f = 0,
            std::int64_t g = 0, std::int64_t h = 0, std::int64_t i = 0,
            std::int64_t j = 0, std::int64_t k = 0, std::int64_t l = 0) {
    IpcFrame frame{};
    frame.type = static_cast<std::uint8_t>(type);
    frame.side = static_cast<std::uint8_t>(side);
    frame.event_kind = static_cast<std::uint8_t>(kind);
    frame.run_id_hash = run_id_hash;
    frame.seq = ++ipc_seq;
    frame.sequence = sequence;
    frame.offset_ms = offset_ms;
    frame.local_steady_us = SteadyUsNow();
    frame.a = a;
    frame.b = b;
    frame.c = c;
    frame.d = d;
    frame.e = e;
    frame.f = f;
    frame.g = g;
    frame.h = h;
    frame.i = i;
    frame.j = j;
    frame.k = k;
    frame.l = l;
    return pipe.WriteFrame(frame);
  }

  void DrainPingTraces() {
#if AE_ENABLE_PING
    for (auto const& rec : g_pending_ping_traces) {
      auto const& e = rec.event;
      IpcFrame frame{};
      frame.type = static_cast<std::uint8_t>(IpcType::kPingTrace);
      frame.side = static_cast<std::uint8_t>(side);
      frame.event_kind = static_cast<std::uint8_t>(e.kind);
      frame.run_id_hash = run_id_hash;
      frame.seq = ++ipc_seq;
      frame.offset_ms =
          e.result_type < 0 ? 0 : static_cast<std::uint32_t>(e.result_type);
      frame.local_steady_us = rec.steady_us;
      frame.a = static_cast<std::int64_t>(e.server_id);
      frame.b = TimePointUs(e.planned_send_at);
      frame.c = TimePointUs(e.actual_send_at);
      frame.d = DurationUs(e.early_by);
      frame.e = DurationUs(e.base_rx_window);
      frame.f = DurationUs(e.effective_wire_rx_window);
      frame.g = TimePointUs(e.required_rx_until);
      frame.h = TimePointUs(e.next_planned_send);
      frame.i = DurationUs(e.ping_guard);
      frame.j = static_cast<std::int64_t>(e.channel_generation);
      frame.k = DurationUs(e.min_rtt);
      frame.l = DurationUs(e.p99_rtt);
      pipe.WriteFrame(frame);
      IpcFrame extra{};
      extra.type = kIpcPingTraceEx;
      extra.side = static_cast<std::uint8_t>(side);
      extra.event_kind = static_cast<std::uint8_t>(e.kind);
      extra.run_id_hash = run_id_hash;
      extra.seq = ++ipc_seq;
      extra.local_steady_us = rec.steady_us;
      extra.a = static_cast<std::int64_t>(e.logical_cycle_id);
      extra.b = static_cast<std::int64_t>(e.physical_attempt_index);
      extra.c = static_cast<std::int64_t>(e.fault_mode);
      extra.d = e.wire_next_connect_ms;
      extra.e = TimePointUs(e.cycle_anchor);
      extra.f = TimePointUs(e.contract_deadline);
      extra.g = TimePointUs(e.next_local_send_at);
      extra.h = e.request_was_sent ? 1 : 0;
      extra.i = e.response_was_ignored ? 1 : 0;
      extra.j = static_cast<std::int64_t>(e.server_id);
      extra.k = rec.qpc;
      extra.l = DurationUs(e.retry_reserve);
      pipe.WriteFrame(extra);
      IpcFrame budget{};
      budget.type = kIpcPingBudget;
      budget.side = static_cast<std::uint8_t>(side);
      budget.event_kind = static_cast<std::uint8_t>(e.kind);
      budget.run_id_hash = run_id_hash;
      budget.seq = ++ipc_seq;
      budget.local_steady_us = rec.steady_us;
      budget.a = DurationUs(e.attempt_lead);
      budget.b = DurationUs(e.retry_reserve);
      budget.c = DurationUs(e.loss_timeout);
      budget.d = e.predeadline_retry_guaranteed ? 1 : 0;
      budget.e = TimePointUs(e.cycle_anchor);
      budget.f = TimePointUs(e.contract_deadline);
      budget.g = DurationUs(e.ping_guard);
      budget.h = static_cast<std::int64_t>(e.logical_cycle_id);
      budget.i = static_cast<std::int64_t>(e.physical_attempt_index);
      budget.j = static_cast<std::int64_t>(e.server_id);
      budget.k = rec.qpc;
      pipe.WriteFrame(budget);
    }
    g_pending_ping_traces.clear();
#endif
  }

  void DrainFaultTraces() {
#if AE_ENABLE_PING_TEST_FAULTS
    for (auto const& rec : g_pending_fault_traces) {
      auto const& e = rec.event;
      IpcFrame frame{};
      frame.type = kIpcFaultTrace;
      frame.side = static_cast<std::uint8_t>(side);
      frame.run_id_hash = run_id_hash;
      frame.seq = ++ipc_seq;
      frame.local_steady_us = rec.steady_us;
      frame.a = static_cast<std::int64_t>(e.server_id);
      frame.b = static_cast<std::int64_t>(e.logical_cycle_id);
      frame.c = static_cast<std::int64_t>(e.physical_attempt_index);
      frame.d = static_cast<std::int64_t>(e.mode);
      frame.e = static_cast<std::int64_t>(e.harness_state);
      frame.f = static_cast<std::int64_t>(e.kind);
      frame.g = e.steady_us;
      pipe.WriteFrame(frame);
    }
    g_pending_fault_traces.clear();
#endif
  }

  void EmitUdpProof(UdpProofPath path, ChannelProof const& proof) {
    if (path == UdpProofPath::kOwn) {
      own_proof = proof;
    } else if (path == UdpProofPath::kDestination) {
      dest_proof = proof;
    }
    IpcFrame f{};
    PackUdpProofFrame(f, path, proof);
    f.side = static_cast<std::uint8_t>(side);
    f.run_id_hash = run_id_hash;
    f.seq = ++ipc_seq;
    f.local_steady_us = SteadyUsNow();
    pipe.WriteFrame(f);
  }

  static bool IsClassifiedWorkProtocol(BenchProtocol protocol) noexcept {
    return protocol == BenchProtocol::kTcp || protocol == BenchProtocol::kUdp;
  }

  void TryEmitOwnProof() {
    if (!client) {
      return;
    }
    (void)client->cloud_connection();
    auto proof = CollectOwnCloudProof(*client);
    if (!proof.present || !IsClassifiedWorkProtocol(proof.protocol)) {
      return;
    }
    if (own_proof_sent && own_proof.protocol == proof.protocol) {
      return;
    }
    own_proof_sent = true;
    EmitUdpProof(UdpProofPath::kOwn, proof);
  }

  void TryEmitDestProof() {
    if (dest_proof_sent || !client || !peer_set) {
      return;
    }
    if (dest_cloud_failed_) {
      dest_cloud_sub.Reset();
      dest_cloud_failed_ = false;
    }
    if (dest_cloud_sub) {
      return;
    }
    auto const now = Now();
    if (dest_retry_at_ && now < *dest_retry_at_) {
      return;
    }
    dest_retry_at_ = now + std::chrono::milliseconds{250};
    auto& get_cloud = client->cloud_manager()->GetCloud(peer_uid);
    dest_cloud_sub = get_cloud.result_event().Subscribe(
        [this](Result<Cloud::ptr, int> const& res) {
          if (!res) {
            dest_cloud_failed_ = true;
            dest_retry_at_ = Now() + std::chrono::milliseconds{250};
            return;
          }
          auto proof = CollectDestinationProofFromCloud(*client, res.value());
          if (!proof.present || !IsClassifiedWorkProtocol(proof.protocol)) {
            dest_cloud_failed_ = true;
            dest_retry_at_ = Now() + std::chrono::milliseconds{250};
            return;
          }
          dest_proof_sent = true;
          EmitUdpProof(UdpProofPath::kDestination, proof);
        });
  }

  void ResetPeerBinding() {
    sample_in_flight = false;
    send_at_.reset();
    requery_at_.reset();
    stream_sub.Reset();
    if (stream) {
      retired_streams_.push_back(std::move(stream));
    }
    new_port_sub.Reset();
    dest_cloud_sub.Reset();
    dest_proof_sent = false;
    dest_cloud_failed_ = false;
    dest_proof = {};
    dest_retry_at_.reset();
  }

  void EnsureStreams() {
    if (!client || !peer_set) {
      return;
    }
    if (side == IpcSide::kA && !stream) {
      stream = std::make_shared<P2pStream>(
          AeContext{*app}, client.Load(), peer_uid,
          client->message_stream_manager().CreatePort(peer_uid));
      stream_sub = stream->out_data_event().Subscribe(
          [this](DataBuffer const& data) { OnReceive(data); });
    }
    if (!new_port_sub) {
      new_port_sub = client->message_stream_manager().new_port_event().Subscribe(
          [this](P2pPortHandle handle) {
            if (handle.destination() != peer_uid && side == IpcSide::kB) {
              // Bob accepts any inbound port from Alice after peer is set.
            }
            stream = std::make_shared<P2pStream>(
                AeContext{*app}, client.Load(), handle.destination(),
                std::move(handle));
            stream_sub = stream->out_data_event().Subscribe(
                [this](DataBuffer const& data) { OnReceive(data); });
          });
    }
  }

  void OnReceive(DataBuffer const& data) {
    auto msg = DeserializeDeliveryBenchMessage(data.data(), data.size());
    if (!msg) {
      Emit(IpcType::kEvent, EventKind::kError, 0, 0, 1);
      return;
    }
    auto& count = seen[msg->sequence];
    ++count;
    auto const recv_qpc = static_cast<std::int64_t>(QpcNow());
    Emit(IpcType::kEvent, EventKind::kSampleReceived, msg->sequence,
         msg->offset_ms, static_cast<std::int64_t>(msg->send_qpc), recv_qpc,
         count);
  }

  // Returns max response sample count across active channels, and fills
  // min/p99 from the channel with the most samples.
  std::size_t CollectResponseStats(Duration* min_out, Duration* p99_out) {
    std::size_t best = 0;
    Duration best_min{};
    Duration best_p99{};
    auto& csc = client->cloud_connection();
    for (auto* sc : csc.servers()) {
      if (sc == nullptr) {
        continue;
      }
      auto* cc = sc->client_connection();
      if (cc == nullptr) {
        continue;
      }
      auto ch = cc->server_connection().current_channel();
      if (!ch) {
        continue;
      }
      auto const& stats = ch->channel_statistics().response_time_statistics();
      if (stats.size() > best) {
        best = stats.size();
        if (!stats.empty()) {
          best_min = stats.min();
          best_p99 = stats.percentile<99>();
        }
      }
    }
    if (min_out != nullptr) {
      *min_out = best_min;
    }
    if (p99_out != nullptr) {
      *p99_out = best_p99;
    }
    return best;
  }

  void PollWarmup() {
    if (!warmup_active || !client) {
      return;
    }
    if (side == IpcSide::kA) {
      EnsureStreams();
      TryEmitOwnProof();
      TryEmitDestProof();
      auto const now = Now();
      if (!warmup_requery_at_ || now >= *warmup_requery_at_) {
        warmup_requery_at_ = now + std::chrono::milliseconds{250};
        if (!query_in_flight_ && peer_set) {
          query_state_only_ = true;
          BeginQuery();
        }
      }

      ServerId server_id{};
      std::size_t sample_count = 0;
      Duration min_rtt{};
      Duration p99_rtt{};
      Protocol protocol = Protocol::kTcp;
      bool present = false;

      if (stream) {
        auto const route = stream->InspectSendRoute();
        if (route.present) {
          present = true;
          server_id = route.server_id;
          sample_count = route.ping_sample_count;
          min_rtt = route.min_rtt;
          p99_rtt = route.p99_rtt;
          protocol = route.protocol;
        }
      }
      // Prefer dest-route stats. Fall back to Alice's own cloud channel so
      // warm-up can complete when dest GetCloud is slow; Q2 may still fail
      // separately and is classified as harness if QueryPeer is unavailable.
      if (sample_count < kWarmupSamples) {
        Duration own_min{};
        Duration own_p99{};
        ServerId own_sid{};
        Protocol own_proto = Protocol::kTcp;
        std::size_t own_n = 0;
        auto& csc = client->cloud_connection();
        for (auto* sc : csc.servers()) {
          if (sc == nullptr) {
            continue;
          }
          auto* cc = sc->client_connection();
          if (cc == nullptr) {
            continue;
          }
          auto ch = cc->server_connection().current_channel();
          if (!ch) {
            continue;
          }
          auto const& stats =
              ch->channel_statistics().response_time_statistics();
          if (stats.size() > own_n) {
            own_n = stats.size();
            own_sid = sc->server_id();
            if (!stats.empty()) {
              own_min = stats.min();
              own_p99 = stats.percentile<99>();
            }
            auto const& props = ch->transport_properties();
            own_proto =
                props.connection_type == ConnectionType::kConnectionLess
                    ? Protocol::kUdp
                    : Protocol::kTcp;
          }
        }
        if (own_n > sample_count) {
          sample_count = own_n;
          min_rtt = own_min;
          p99_rtt = own_p99;
          if (!present) {
            present = true;
            server_id = own_sid;
            protocol = own_proto;
          }
          if (own_proof.present) {
            server_id = own_proof.server_id;
            protocol = own_proof.protocol == BenchProtocol::kUdp
                           ? Protocol::kUdp
                           : Protocol::kTcp;
          }
        }
      }

      static std::size_t last_logged = 0;
      if (sample_count != last_logged &&
          (sample_count % 2 == 0 || sample_count >= kWarmupSamples)) {
        last_logged = sample_count;
        std::cerr << "Alice dest warmup server=" << server_id
                  << " samples=" << sample_count
                  << " present=" << present << std::endl;
      }
      if (!present || sample_count < kWarmupSamples) {
        return;
      }
      auto const min_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(min_rtt)
              .count();
      auto const p99_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(p99_rtt)
              .count();
      if ((min_ms == 200 && p99_ms == 200) || min_ms == 5000 || p99_ms == 5000) {
        std::cerr << "Alice dest warmup stats look synthetic: min=" << min_ms
                  << " p99=" << p99_ms << "\n";
        return;
      }
      warmup_active = false;
      warmup_requery_at_.reset();
      std::cout << "## Alice dest-server ping statistics (child)\n"
                << "server_id=" << server_id
                << " samples=" << sample_count
                << " min_rtt_ms=" << min_ms << " p99_rtt_ms=" << p99_ms
                << " protocol="
                << (protocol == Protocol::kUdp ? "udp" : "tcp") << "\n";
      Emit(IpcType::kWarmupDone, EventKind::kWarmupDone, 0, 0,
           static_cast<std::int64_t>(sample_count), min_ms, p99_ms,
           static_cast<std::int64_t>(server_id),
           BenchProtocolFromAe(protocol));
      return;
    }
    if (side != IpcSide::kB) {
      return;
    }
    Duration min_rtt{};
    Duration p99_rtt{};
    auto const n = CollectResponseStats(&min_rtt, &p99_rtt);
    static std::size_t last_logged = 0;
    if (n != last_logged && (n % 2 == 0 || n >= kWarmupSamples)) {
      last_logged = n;
      std::cerr << "Bob warmup samples=" << n << std::endl;
    }
    if (n < kWarmupSamples) {
      return;
    }
    auto const interval = std::chrono::duration_cast<Duration>(
        std::chrono::milliseconds{ping_interval_ms});
    auto const guard = ClampPingSendGuard(
        ComputePingSendGuard(min_rtt, p99_rtt), interval);
    auto const min_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(min_rtt).count();
    auto const p99_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(p99_rtt).count();
    auto const guard_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(guard).count();
    // Reject obvious synthetic seed values (200ms estimate / 5000ms).
    if ((min_ms == 200 && p99_ms == 200) || min_ms == 5000 || p99_ms == 5000) {
      std::cerr << "warmup stats look synthetic: min=" << min_ms
                << " p99=" << p99_ms << "\n";
      return;
    }
    warmup_active = false;
    std::cout << "## Bob ping statistics (child)\n"
              << "samples=" << n << " min_rtt_ms=" << min_ms
              << " p99_rtt_ms=" << p99_ms << " guard_ms=" << guard_ms << "\n";
    // sequence unused; offset_ms carries guard_ms; a=n b=min c=p99
    Emit(IpcType::kWarmupDone, EventKind::kWarmupDone, 0,
         static_cast<std::uint32_t>(guard_ms), static_cast<std::int64_t>(n),
         min_ms, p99_ms);
  }

  void StartSample(std::uint32_t sequence, std::int64_t offset_ms) {
    if (side != IpcSide::kA || !client || !peer_set) {
      return;
    }
    sample_in_flight = true;
    query_state_only_ = false;
    pending_sequence = sequence;
    pending_offset_signed_ = offset_ms;
    pending_offset_ms = offset_ms < 0 ? 0u : static_cast<std::uint32_t>(offset_ms);
    send_at_.reset();
    requery_at_.reset();
    EnsureStreams();
    BeginQuery();
  }

  void BeginQuery() {
    // Reset subscription before replacing Client-owned action.
    query_sub.Reset();
    query_in_flight_ = true;
    ++query_created_;
    auto& action = client->QueryPeerPresence(peer_uid);
    query_sub = action.result_event().Subscribe(
        [this, &action](Result<PeerPresence, int> const& res) {
          last_diagnostics_ = action.server_diagnostics();
          last_coverage_ = action.coverage();
          query_in_flight_ = false;
          if (query_state_only_) {
            EmitScheduleState(res);
            query_state_only_ = false;
            return;
          }
          OnSchedule(res);
        });
    if (action.is_finished()) {
      // Synchronous completion can race Subscribe; clear the stuck flag.
      query_in_flight_ = false;
    }
  }

  ServerTimingDiagnostic const* FindDestDiagnostic(
      ServerId server_id) const {
    for (auto const& d : last_diagnostics_) {
      if (d.server_id == server_id && d.has_raw &&
          d.status == ServerTimingAttemptStatus::kSuccess) {
        return &d;
      }
    }
    return nullptr;
  }

  void OnSchedule(Result<PeerPresence, int> const& res) {
    if (!res) {
      sample_in_flight = false;
      send_at_.reset();
      requery_at_.reset();
      Emit(IpcType::kSampleResult, EventKind::kError, pending_sequence,
           pending_offset_ms, res.error());
      return;
    }
    TryEmitDestProof();
    EnsureStreams();
    if (!stream) {
      sample_in_flight = false;
      Emit(IpcType::kSampleResult, EventKind::kError, pending_sequence,
           pending_offset_ms, 3);
      return;
    }
    auto const route = stream->InspectSendRoute();
    if (!route.present) {
      sample_in_flight = false;
      Emit(IpcType::kSampleResult, EventKind::kSampleSkipped, pending_sequence,
           pending_offset_ms, 0, -1, 7);
      return;
    }
    if (route.ping_sample_count < kWarmupSamples) {
      requery_at_ = Now() + std::chrono::milliseconds{250};
      return;
    }
    auto const* diag = FindDestDiagnostic(route.server_id);
    if (diag == nullptr ||
        diag->converted.state != PeerScheduleState::kExpected ||
        !diag->converted.next_ping_deadline.has_value()) {
      sample_in_flight = false;
      send_at_.reset();
      requery_at_.reset();
      Emit(IpcType::kSampleResult, EventKind::kSampleSkipped, pending_sequence,
           pending_offset_ms, 0, -1, 8);
      return;
    }

    auto const cycle_start =
        *diag->converted.next_ping_deadline -
        std::chrono::duration_cast<Duration>(
            std::chrono::milliseconds{ping_interval_ms});
    auto const target = AddOffsetMs(cycle_start, pending_offset_signed_);
    auto const now = Now();
    pending_last_us_ = TimePointUs(cycle_start);
    pending_next_us_ = TimePointUs(*diag->converted.next_ping_deadline);
    pending_schedule_server_id_ = static_cast<std::int64_t>(route.server_id);
    pending_route_generation_ =
        static_cast<std::int64_t>(route.route_generation);
    pending_protocol_ = BenchProtocolFromAe(route.protocol);
    pending_raw_delta_ms_ = diag->raw.next_ping_delta_ms;
    pending_last_connect_ms_ = diag->raw.last_connect_delta_ms;
    pending_qsend_us_ = TimePointUs(diag->qsend);
    pending_one_way_us_ = DurationUs(diag->one_way);
    pending_target_us_ = TimePointUs(target);

    auto const to_next_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            *diag->converted.next_ping_deadline - now)
            .count();
    std::cerr << "Alice dest-server schedule server=" << route.server_id
              << " gen=" << route.route_generation
              << " age_to_cycle_start_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     now - cycle_start)
                     .count()
              << " to_target_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(target -
                                                                       now)
                     .count()
              << " to_next_ms=" << to_next_ms
              << " offset_ms=" << pending_offset_signed_
              << " raw_delta_ms=" << pending_raw_delta_ms_ << std::endl;

    if (now + kSkipIfCloserThan > target && now < target) {
      sample_in_flight = false;
      Emit(IpcType::kSampleResult, EventKind::kSampleSkipped, pending_sequence,
           pending_offset_ms, pending_last_us_, pending_next_us_, 1,
           pending_schedule_server_id_, pending_schedule_server_id_,
           pending_route_generation_, pending_protocol_, pending_raw_delta_ms_,
           pending_last_connect_ms_);
      return;
    }
    if (now >= target) {
      auto const next_target = AddOffsetMs(*diag->converted.next_ping_deadline,
                                           pending_offset_signed_);
      if (next_target > now + kSkipIfCloserThan) {
        auto const delay_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(next_target -
                                                                  now)
                .count();
        if (delay_ms > 15000) {
          sample_in_flight = false;
          Emit(IpcType::kSampleResult, EventKind::kSampleSkipped,
               pending_sequence, pending_offset_ms, pending_last_us_,
               pending_next_us_, 3, pending_schedule_server_id_,
               pending_schedule_server_id_, pending_route_generation_);
          return;
        }
        pending_last_us_ = TimePointUs(*diag->converted.next_ping_deadline);
        pending_next_us_ = TimePointUs(
            *diag->converted.next_ping_deadline +
            std::chrono::duration_cast<Duration>(
                std::chrono::milliseconds{ping_interval_ms}));
        pending_target_us_ = TimePointUs(next_target);
        send_at_ = next_target;
        return;
      }
      auto wait_until =
          *diag->converted.next_ping_deadline + std::chrono::milliseconds{150};
      if (wait_until <= now) {
        wait_until = now + std::chrono::milliseconds{250};
      }
      if (wait_until > now + std::chrono::seconds{15}) {
        sample_in_flight = false;
        Emit(IpcType::kSampleResult, EventKind::kSampleSkipped,
             pending_sequence, pending_offset_ms, pending_last_us_,
             pending_next_us_, 2, pending_schedule_server_id_,
             pending_schedule_server_id_, pending_route_generation_);
        return;
      }
      requery_at_ = wait_until;
      return;
    }

    auto const delay_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(target - now)
            .count();
    if (delay_ms > 15000) {
      sample_in_flight = false;
      Emit(IpcType::kSampleResult, EventKind::kSampleSkipped, pending_sequence,
           pending_offset_ms, pending_last_us_, pending_next_us_, 3,
           pending_schedule_server_id_, pending_schedule_server_id_,
           pending_route_generation_);
      return;
    }
    send_at_ = target;
  }

  void PollSampleTiming() {
    if (side != IpcSide::kA || !sample_in_flight || !client) {
      return;
    }
    auto const now = Now();
    if (requery_at_ && now >= *requery_at_) {
      requery_at_.reset();
      BeginQuery();
      return;
    }
    if (send_at_ && now >= *send_at_) {
      send_at_.reset();
      SendPendingMessage();
    }
  }

  void SendPendingMessage() {
    if (!stream) {
      EnsureStreams();
    }
    if (!stream) {
      sample_in_flight = false;
      Emit(IpcType::kSampleResult, EventKind::kError, pending_sequence,
           pending_offset_ms, 3);
      return;
    }
    auto const before = stream->InspectSendRoute();
    if (!before.present ||
        static_cast<std::int64_t>(before.server_id) !=
            pending_schedule_server_id_ ||
        static_cast<std::int64_t>(before.route_generation) !=
            pending_route_generation_) {
      sample_in_flight = false;
      Emit(IpcType::kSampleResult, EventKind::kSampleSkipped, pending_sequence,
           pending_offset_ms, pending_last_us_, pending_next_us_, 6,
           pending_schedule_server_id_,
           before.present ? static_cast<std::int64_t>(before.server_id) : 0,
           before.present ? static_cast<std::int64_t>(before.route_generation)
                          : 0,
           before.present ? BenchProtocolFromAe(before.protocol) : 0);
      return;
    }
    auto const dest_proto = static_cast<BenchProtocol>(pending_protocol_);
    if (RefuseTcpSample(own_proof.protocol, dest_proto)) {
      sample_in_flight = false;
      Emit(IpcType::kSampleResult, EventKind::kSampleSkipped, pending_sequence,
           pending_offset_ms, pending_last_us_, pending_next_us_, 5,
           pending_schedule_server_id_, pending_schedule_server_id_,
           pending_route_generation_, pending_protocol_);
      return;
    }

    DeliveryBenchMessage msg{};
    msg.offset_ms = static_cast<std::uint16_t>(pending_offset_ms);
    msg.sequence = pending_sequence;
    msg.send_qpc = QpcNow();
    auto bytes = SerializeDeliveryBenchMessage(msg);
    DataBuffer payload{bytes.begin(), bytes.end()};
    stream->Write(std::move(payload));
    auto const after = stream->LastSendRoute();
    auto const actual_id = after.present
                               ? static_cast<std::int64_t>(after.server_id)
                               : pending_schedule_server_id_;
    auto const actual_gen =
        after.present ? static_cast<std::int64_t>(after.route_generation)
                      : pending_route_generation_;
    auto const actual_proto =
        after.present ? BenchProtocolFromAe(after.protocol) : pending_protocol_;
    sample_in_flight = false;
    Emit(IpcType::kSampleResult, EventKind::kSampleSent, pending_sequence,
         pending_offset_ms, pending_last_us_, pending_next_us_,
         static_cast<std::int64_t>(msg.send_qpc), pending_schedule_server_id_,
         actual_id, actual_gen, actual_proto, pending_raw_delta_ms_,
         pending_last_connect_ms_, pending_qsend_us_, pending_one_way_us_,
         pending_target_us_);
    std::cerr << "Alice sent seq=" << pending_sequence
              << " offset_ms=" << pending_offset_ms
              << " schedule_server=" << pending_schedule_server_id_
              << " actual_server=" << actual_id << std::endl;
  }


  void SendTaggedNow(std::uint32_t tag) {
    EnsureStreams();
    if (!stream) {
      Emit(IpcType::kSampleResult, EventKind::kError, tag, 0, 3);
      return;
    }
    auto const before = stream->InspectSendRoute();
    if (!before.present) {
      Emit(IpcType::kSampleResult, EventKind::kSampleSkipped, tag, 0, 0, 0, 7);
      return;
    }
    DeliveryBenchMessage msg{};
    msg.offset_ms = 0;
    msg.sequence = tag;
    msg.send_qpc = QpcNow();
    auto bytes = SerializeDeliveryBenchMessage(msg);
    DataBuffer payload{bytes.begin(), bytes.end()};
    auto const dest = static_cast<std::int64_t>(before.server_id);
    auto const gen = static_cast<std::int64_t>(before.route_generation);
    stream->Write(std::move(payload));
    auto const after = stream->LastSendRoute();
    if (after.present && (static_cast<std::int64_t>(after.server_id) != dest ||
                          static_cast<std::int64_t>(after.route_generation) != gen)) {
      Emit(IpcType::kSampleResult, EventKind::kSampleSkipped, tag, 0, 0, 0, 6,
           dest, static_cast<std::int64_t>(after.server_id), gen);
      return;
    }
    Emit(IpcType::kSampleResult, EventKind::kSampleSent, tag, 0, 0, 0,
         static_cast<std::int64_t>(msg.send_qpc), dest, dest, gen,
         BenchProtocolFromAe(before.protocol));
  }

  void ArmFault(IpcFrame const& f) {
#if AE_ENABLE_PING_TEST_FAULTS
    PingFaultPlan plan{};
    plan.server_id = static_cast<ServerId>(f.a);
    plan.logical_cycle_id = 0;
    plan.physical_attempt_index =
        f.b <= 0 ? 1u : static_cast<std::uint32_t>(f.b);
    plan.mode = static_cast<PingFaultMode>(f.c);
    if (f.d > 0) {
      plan.timeout_override = Duration{static_cast<Duration::rep>(f.d)};
    }
    if (f.f > 0) {
      plan.logical_cycle_id = static_cast<std::uint64_t>(f.f);
    }
    if (f.e == 0) {
      PingTestFaults::Instance().Clear();
      PingFaultTraceEvent cleared{};
      cleared.kind = PingFaultTraceKind::kCleared;
      cleared.harness_state = PingFaultHarnessState::kIdle;
      cleared.steady_us = SteadyUsNow();
      g_pending_fault_traces.push_back({cleared, SteadyUsNow()});
    }
    if (f.h != 0) {
      plan.hold_enabled = true;
      plan.retry_hold_offset_us = f.g;
    }
    if (plan.mode != PingFaultMode::kNone) {
      PingTestFaults::Instance().Arm(plan);
      PingFaultTraceEvent armed{};
      armed.kind = PingFaultTraceKind::kArmed;
      armed.server_id = plan.server_id;
      armed.logical_cycle_id = plan.logical_cycle_id;
      armed.physical_attempt_index = plan.physical_attempt_index;
      armed.mode = plan.mode;
      armed.harness_state = PingFaultHarnessState::kArmed;
      armed.steady_us = SteadyUsNow();
      g_pending_fault_traces.push_back({armed, SteadyUsNow()});
    }
    DrainFaultTraces();
    Emit(IpcType::kAck, EventKind::kAck, 0, 0, f.a, f.b, f.c);
#else
    (void)f;
    Emit(IpcType::kEvent, EventKind::kError, 0, 0, 12);
#endif
  }

  void EmitScheduleState(Result<PeerPresence, int> const& res) {
    IpcFrame frame{};
    frame.type = kIpcScheduleState;
    frame.side = static_cast<std::uint8_t>(side);
    frame.run_id_hash = run_id_hash;
    frame.seq = ++ipc_seq;
    frame.local_steady_us = SteadyUsNow();
    if (!res) {
      frame.a = -1;
      frame.b = res.error();
    } else {
      frame.a = static_cast<std::int64_t>(res.value().state);
      frame.b = res.value().next_ping_deadline.has_value()
                    ? TimePointUs(*res.value().next_ping_deadline)
                    : 0;
      frame.c = res.value().last_online.has_value()
                    ? TimePointUs(*res.value().last_online)
                    : 0;
    }
    frame.d = static_cast<std::int64_t>(last_coverage_.selected_server_count);
    frame.e = static_cast<std::int64_t>(last_coverage_.queried_server_count);
    frame.f = static_cast<std::int64_t>(last_coverage_.successful_server_count);
    frame.g = static_cast<std::int64_t>(last_coverage_.failed_server_count);
    frame.h =
        static_cast<std::int64_t>(last_coverage_.quarantined_skipped_count);
    frame.i = static_cast<std::int64_t>(QpcNow());
    frame.j = pending_query_checkpoint_;
    frame.k = std::numeric_limits<std::int64_t>::min();
    frame.l = std::numeric_limits<std::int64_t>::min();
    for (auto const& d : last_diagnostics_) {
      if (d.has_raw &&
          d.status == ServerTimingAttemptStatus::kSuccess) {
        frame.k = d.raw.next_ping_delta_ms;
        frame.l = d.raw.last_connect_delta_ms;
        break;
      }
    }
    pipe.WriteFrame(frame);
    EmitQueryStats();
  }

  void EmitQueryStats() {
    IpcFrame stats{};
    stats.type = kIpcQueryStats;
    stats.side = static_cast<std::uint8_t>(side);
    stats.run_id_hash = run_id_hash;
    stats.seq = ++ipc_seq;
    stats.local_steady_us = SteadyUsNow();
    stats.a = query_attempts_;
    stats.b = query_created_;
    stats.c = query_reused_;
    stats.d = query_skipped_inflight_;
    stats.e = query_extra_subscribers_;
    stats.f = pending_query_checkpoint_;
    stats.i = static_cast<std::int64_t>(QpcNow());
    pipe.WriteFrame(stats);
  }

  void QueryNow(std::int64_t checkpoint, bool force) {
    if (!client || !peer_set) {
      return;
    }
    ++query_attempts_;
    pending_query_checkpoint_ = checkpoint;
    if (query_in_flight_) {
      ++query_skipped_inflight_;
      auto& existing = client->QueryPeerPresence(peer_uid);
      if (existing.is_finished()) {
        query_in_flight_ = false;
      } else {
        ++query_reused_;
        if (force) {
          ++query_extra_subscribers_;
          extra_query_sub_.Reset();
          extra_query_sub_ = existing.result_event().Subscribe(
              [this](Result<PeerPresence, int> const& res) {
                EmitScheduleState(res);
              });
        }
        EmitQueryStats();
        return;
      }
    }
    query_state_only_ = true;
    BeginQuery();
  }

  void StartAnnounceUnknown() {
    if (!client) {
      Emit(IpcType::kEvent, EventKind::kError, 0, 0, 13);
      return;
    }
    announce_sub.Reset();
    auto& action = client->AnnounceNextPingUnknown();
    announce_sub = action.result_event().Subscribe(
        [this](Result<std::monostate, int> const& res) {
          Emit(IpcType::kAck, EventKind::kAck, 0, 0, res ? 0 : res.error(),
               static_cast<std::int64_t>(QpcNow()));
        });
  }
  void HandleIpc(IpcFrame const& f) {
    auto const type = static_cast<IpcType>(f.type);
    switch (type) {
      case IpcType::kSetPeerUid: {
        auto const next = UidFromHalves(f.a, f.b);
        if (!peer_set || next != peer_uid) {
          ResetPeerBinding();
          std::cerr << (side == IpcSide::kA ? "Alice" : "Bob")
                    << " peer uid changed; rebuilt P2P binding" << std::endl;
        }
        peer_uid = next;
        peer_set = true;
        EnsureStreams();
        Emit(IpcType::kAck, EventKind::kAck);
        break;
      }
      case IpcType::kWaitWarmup:
        warmup_active = true;
        warmup_requery_at_.reset();
        std::cerr << (side == IpcSide::kA ? "Alice" : "Bob")
                  << " WaitWarmup received" << std::endl;
        TryEmitOwnProof();
        TryEmitDestProof();
        break;
      case IpcType::kRunSample: {
        auto offset = static_cast<std::int64_t>(f.offset_ms);
        if (f.a != 0) {
          offset = f.a;
        }
        StartSample(f.sequence, offset);
        break;
      }
      case IpcType::kShutdown:
        exit_requested = true;
        Emit(IpcType::kAck, EventKind::kAck);
        break;
      default:
        if (f.type == kIpcArmFault) {
          ArmFault(f);
        } else if (f.type == kIpcSendTagged) {
          SendTaggedNow(f.sequence);
        } else if (f.type == kIpcQueryNow) {
          QueryNow(f.a, f.c != 0);
        } else if (f.type == kIpcAnnounceUnknown) {
          StartAnnounceUnknown();
        }
        break;
    }
  }
};

std::unique_ptr<AetherApp> MakeApp(std::string const& state_dir) {
  auto dir = std::make_shared<std::string>(state_dir);
  return AetherApp::Construct(
      AetherAppContext{[dir]() {
        return std::unique_ptr<IDomainStorage>{
            std::make_unique<DirectoryDomainStorage>(*dir)};
      }}
#if AE_DISTILLATION
          .AddAdapterFactory([](AetherAppContext const& context) {
            return EthernetAdapter::ptr::Create(
                CreateWith{context.domain()}.with_id(
                    GlobalId::kEthernetAdapter),
                context.aether(), context.poller(), context.dns_resolver());
          })
#endif
  );
}

}  // namespace

int RunClientRole(ClientArgs const& args) {
  RoleState state;
  state.side = static_cast<int>(args.side) == 1 ? IpcSide::kB : IpcSide::kA;
  state.ping_interval_ms = args.ping_interval_ms;
  state.receive_window_ms = args.receive_window_ms;
  state.run_id_hash = HashRunId(args.run_id);

  if (!state.pipe.Connect(args.pipe_name, 60000)) {
    std::cerr << "pipe connect failed: " << args.pipe_name << "\n";
    return 2;
  }

  state.app = MakeApp(args.state_dir);
  auto parent = Uid::FromString(args.parent_uid);
  auto& select =
      state.app->aether()->SelectClient(parent, args.client_name);
  state.select_sub = select.result_event().Subscribe(
      [&](Result<Client::ptr, int> const& res) {
        if (!res) {
          state.Emit(IpcType::kEvent, EventKind::kError, 0, 0, 10);
          state.exit_requested = true;
          return;
        }
        state.client = res.value();
        if (state.side == IpcSide::kB) {
          auto const ping_ms = state.ping_interval_ms;
          auto const rx_ms = state.receive_window_ms;
          std::cerr << "Bob SetReceiveSchedule applying ping_interval_ms="
                    << ping_ms << " receive_window_ms=" << rx_ms << std::endl;
          auto ok = state.client->SetReceiveSchedule(ReceiveSchedule{
              .ping_interval = std::chrono::duration_cast<Duration>(
                  std::chrono::milliseconds{state.ping_interval_ms}),
              .receive_window = std::chrono::duration_cast<Duration>(
                  std::chrono::milliseconds{state.receive_window_ms}),
          });
          std::cerr << "Bob SetReceiveSchedule done ok=" << static_cast<bool>(ok)
                    << " (expect ping_interval_ms=" << ping_ms
                    << " receive_window_ms=" << rx_ms << ")" << std::endl;
          if (!ok) {
            state.Emit(IpcType::kEvent, EventKind::kError, 0, 0, 11);
            state.exit_requested = true;
            return;
          }
#if AE_ENABLE_PING
          SetPingTraceHook(&OnPingTrace);
#endif
#if AE_ENABLE_PING_TEST_FAULTS
          SetPingFaultTraceHook(&OnPingFaultTrace);
#endif
        }
        state.client_ready = true;
        std::int64_t lo = 0;
        std::int64_t hi = 0;
        UidToHalves(state.client->uid(), lo, hi);
        std::cerr << "Client ready side="
                  << (state.side == IpcSide::kA ? "A" : "B") << std::endl;
        state.Emit(IpcType::kUidReport, EventKind::kChildReady, 0, 0, lo, hi);
        state.Emit(IpcType::kChildReady, EventKind::kChildReady);
      });

  while (!state.exit_requested && !state.app->IsExited()) {
    auto const now = Now();
    auto next = state.app->Update(now);
    state.DrainPingTraces();
    state.DrainFaultTraces();
    if (auto frame = state.pipe.TryReadFrame(0)) {
      state.HandleIpc(*frame);
    }
    state.TryEmitOwnProof();
    state.TryEmitDestProof();
    state.PollWarmup();
    state.PollSampleTiming();
    state.app->WaitUntil(
        std::min(next, now + std::chrono::milliseconds{5}));
  }
#if AE_ENABLE_PING
  SetPingTraceHook(nullptr);
  if (static_cast<int>(args.side) == 1 && !g_all_ping_traces.empty()) {
    std::ofstream csv(args.state_dir + "/bob_ping_trace.csv");
    csv << "kind,server_id,planned_send_us,actual_send_us,early_by_us,"
           "base_rx_window_us,effective_wire_rx_window_us,required_rx_until_us,"
           "next_planned_send_us,ping_guard_us,min_rtt_us,p99_rtt_us,"
           "channel_generation,result_type,steady_us\n";
    for (auto const& rec : g_all_ping_traces) {
      auto const& e = rec.event;
      csv << static_cast<int>(e.kind) << ","
          << static_cast<std::int64_t>(e.server_id) << ","
          << TimePointUs(e.planned_send_at) << ","
          << TimePointUs(e.actual_send_at) << "," << DurationUs(e.early_by)
          << "," << DurationUs(e.base_rx_window) << ","
          << DurationUs(e.effective_wire_rx_window) << ","
          << TimePointUs(e.required_rx_until) << ","
          << TimePointUs(e.next_planned_send) << ","
          << DurationUs(e.ping_guard) << "," << DurationUs(e.min_rtt) << ","
          << DurationUs(e.p99_rtt) << "," << e.channel_generation << ","
          << e.result_type << "," << rec.steady_us << "\n";
    }
  }
#endif
  return 0;
}

}  // namespace ae::test_uap_ping_retry_window
