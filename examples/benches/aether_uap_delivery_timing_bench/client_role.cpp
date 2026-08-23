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
#include "aether/ae_actions/query_peer_receive_schedule.h"
#include "aether/channels/channel.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/cloud_connections/ping_schedule_guard.h"
#include "aether/receive_schedule.h"
#include "aether/server_connections/server_connection.h"

#include "common/bench_ipc.h"
#include "common/bench_message.h"
#include "common/directory_domain_storage.h"
#include "common/udp_proof.h"

namespace ae::bench::uap {
namespace {

constexpr auto kBobPingInterval = std::chrono::milliseconds{3000};
constexpr auto kBobReceiveWindow = std::chrono::milliseconds{1000};
constexpr auto kSkipIfCloserThan = std::chrono::milliseconds{50};
constexpr std::size_t kWarmupSamples = 10;

inline std::int64_t TimePointUs(TimePoint tp) {
  return std::chrono::duration_cast<std::chrono::microseconds>(
             tp.time_since_epoch())
      .count();
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

inline std::uint64_t QpcNow() {
  LARGE_INTEGER v{};
  QueryPerformanceCounter(&v);
  return static_cast<std::uint64_t>(v.QuadPart);
}

struct RoleState {
  Side side{};
  std::uint32_t run_id_hash{0};
  NamedPipeClient pipe;
  std::unique_ptr<AetherApp> app;
  Client::ptr client;
  Uid peer_uid{};
  bool peer_set{false};
  std::shared_ptr<P2pStream> stream;
  Subscription stream_sub;
  Subscription new_port_sub;
  Subscription select_sub;
  Subscription query_sub;
  Subscription dest_cloud_sub;
  bool dest_proof_sent{false};
  bool own_proof_sent{false};
  std::optional<TimePoint> dest_retry_at_{};
  ChannelProof own_proof{};
  ChannelProof dest_proof{};
  std::unordered_map<std::uint32_t, int> seen;
  std::uint32_t ipc_seq{0};
  bool exit_requested{false};
  bool client_ready{false};
  bool warmup_active{false};
  bool sample_in_flight{false};
  std::uint32_t pending_sequence{0};
  std::uint32_t pending_offset_ms{0};
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
    auto const now = Now();
    if (dest_retry_at_ && now < *dest_retry_at_) {
      return;
    }
    dest_retry_at_ = now + std::chrono::milliseconds{250};
    dest_cloud_sub.Reset();
    auto& get_cloud = client->cloud_manager()->GetCloud(peer_uid);
    dest_cloud_sub = get_cloud.result_event().Subscribe(
        [this](Result<Cloud::ptr, int> const& res) {
          if (!res) {
            return;
          }
          auto proof = CollectDestinationProofFromCloud(*client, res.value());
          if (!proof.present || !IsClassifiedWorkProtocol(proof.protocol)) {
            return;
          }
          dest_proof_sent = true;
          EmitUdpProof(UdpProofPath::kDestination, proof);
        });
  }

  void EnsureStreams() {
    if (!client || !peer_set) {
      return;
    }
    if (side == Side::kA && !stream) {
      stream = std::make_shared<P2pStream>(
          AeContext{*app}, client.Load(), peer_uid,
          client->message_stream_manager().CreatePort(peer_uid));
      stream_sub = stream->out_data_event().Subscribe(
          [this](DataBuffer const& data) { OnReceive(data); });
    }
    if (!new_port_sub) {
      new_port_sub = client->message_stream_manager().new_port_event().Subscribe(
          [this](P2pPortHandle handle) {
            if (handle.destination() != peer_uid && side == Side::kB) {
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
    if (side == Side::kA) {
      EnsureStreams();
      if (!stream) {
        return;
      }
      auto const route = stream->InspectSendRoute();
      static std::size_t last_logged = 0;
      if (route.ping_sample_count != last_logged &&
          (route.ping_sample_count % 2 == 0 ||
           route.ping_sample_count >= kWarmupSamples)) {
        last_logged = route.ping_sample_count;
        std::cerr << "Alice dest warmup server=" << route.server_id
                  << " samples=" << route.ping_sample_count
                  << " present=" << route.present << std::endl;
      }
      if (!route.present || route.ping_sample_count < kWarmupSamples) {
        return;
      }
      auto const min_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(route.min_rtt)
              .count();
      auto const p99_ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(route.p99_rtt)
              .count();
      if ((min_ms == 200 && p99_ms == 200) || min_ms == 5000 || p99_ms == 5000) {
        std::cerr << "Alice dest warmup stats look synthetic: min=" << min_ms
                  << " p99=" << p99_ms << "\n";
        return;
      }
      warmup_active = false;
      std::cout << "## Alice dest-server ping statistics (child)\n"
                << "server_id=" << route.server_id
                << " samples=" << route.ping_sample_count
                << " min_rtt_ms=" << min_ms << " p99_rtt_ms=" << p99_ms
                << " protocol="
                << (route.protocol == Protocol::kUdp ? "udp" : "tcp") << "\n";
      Emit(IpcType::kWarmupDone, EventKind::kWarmupDone, 0, 0,
           static_cast<std::int64_t>(route.ping_sample_count), min_ms, p99_ms,
           static_cast<std::int64_t>(route.server_id),
           BenchProtocolFromAe(route.protocol));
      return;
    }
    if (side != Side::kB) {
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
    auto const interval =
        std::chrono::duration_cast<Duration>(kBobPingInterval);
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

  void StartSample(std::uint32_t sequence, std::uint32_t offset_ms) {
    if (side != Side::kA || !client || !peer_set) {
      return;
    }
    // Coordinator may time out while we are waiting to requery; always take
    // the latest sample request.
    sample_in_flight = true;
    pending_sequence = sequence;
    pending_offset_ms = offset_ms;
    send_at_.reset();
    requery_at_.reset();
    EnsureStreams();
    BeginQuery();
  }

  void BeginQuery() {
    // Reset subscription before replacing Client-owned action.
    query_sub.Reset();
    auto& action = client->QueryPeerReceiveSchedule(peer_uid);
    query_sub = action.result_event().Subscribe(
        [this, &action](Result<PeerReceiveSchedule, int> const& res) {
          last_diagnostics_ = action.server_diagnostics();
          OnSchedule(res);
        });
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

  void OnSchedule(Result<PeerReceiveSchedule, int> const& res) {
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

    auto const offset = std::chrono::milliseconds{pending_offset_ms};
    auto const cycle_start =
        *diag->converted.next_ping_deadline -
        std::chrono::duration_cast<Duration>(kBobPingInterval);
    auto const target = cycle_start + offset;
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
              << " offset_ms=" << pending_offset_ms
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
    if (side != Side::kA || !sample_in_flight || !client) {
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

  void HandleIpc(IpcFrame const& f) {
    auto const type = static_cast<IpcType>(f.type);
    switch (type) {
      case IpcType::kSetPeerUid:
        peer_uid = UidFromHalves(f.a, f.b);
        peer_set = true;
        if (client) {
          // Bob: schedule already applied; Alice: default schedule.
          TryEmitOwnProof();
          TryEmitDestProof();
        }
        EnsureStreams();
        Emit(IpcType::kAck, EventKind::kAck);
        break;
      case IpcType::kWaitWarmup:
        warmup_active = true;
        std::cerr << (side == Side::kA ? "Alice" : "Bob")
                  << " WaitWarmup received" << std::endl;
        TryEmitOwnProof();
        TryEmitDestProof();
        break;
      case IpcType::kRunSample:
        StartSample(f.sequence, f.offset_ms);
        break;
      case IpcType::kShutdown:
        exit_requested = true;
        Emit(IpcType::kAck, EventKind::kAck);
        break;
      default:
        break;
    }
  }
};

std::unique_ptr<AetherApp> MakeApp(std::string const& state_dir) {
  return AetherApp::Construct(
      AetherAppContext{[state_dir]() {
        return std::unique_ptr<IDomainStorage>{
            std::make_unique<DirectoryDomainStorage>(state_dir)};
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
  state.side = args.side;
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
        if (state.side == Side::kB) {
          auto const ping_ms = kBobPingInterval.count();
          auto const rx_ms = kBobReceiveWindow.count();
          std::cerr << "Bob SetReceiveSchedule applying ping_interval_ms="
                    << ping_ms << " receive_window_ms=" << rx_ms << std::endl;
          auto ok = state.client->SetReceiveSchedule(ReceiveSchedule{
              .ping_interval =
                  std::chrono::duration_cast<Duration>(kBobPingInterval),
              .receive_window =
                  std::chrono::duration_cast<Duration>(kBobReceiveWindow),
          });
          std::cerr << "Bob SetReceiveSchedule done ok=" << static_cast<bool>(ok)
                    << " (expect ping_interval_ms=" << ping_ms
                    << " receive_window_ms=" << rx_ms << ")" << std::endl;
          if (!ok) {
            state.Emit(IpcType::kEvent, EventKind::kError, 0, 0, 11);
            state.exit_requested = true;
            return;
          }
        }
        state.client_ready = true;
        std::int64_t lo = 0;
        std::int64_t hi = 0;
        UidToHalves(state.client->uid(), lo, hi);
        std::cerr << "Client ready side="
                  << (state.side == Side::kA ? "A" : "B") << std::endl;
        state.Emit(IpcType::kUidReport, EventKind::kChildReady, 0, 0, lo, hi);
        state.Emit(IpcType::kChildReady, EventKind::kChildReady);
      });

  while (!state.exit_requested && !state.app->IsExited()) {
    auto const now = Now();
    auto next = state.app->Update(now);
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
  return 0;
}

}  // namespace ae::bench::uap
