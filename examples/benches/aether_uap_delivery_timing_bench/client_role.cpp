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
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <unordered_map>
#include <utility>

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
  ChannelProof own_proof_{};
  ChannelProof dest_proof_{};
  bool own_proof_sent_{false};
  bool dest_proof_sent_{false};
  bool no_udp_endpoint_sent_{false};
  bool waiting_cloud_link_{false};
  TimePoint cloud_wait_deadline_{};
  std::uint64_t warmup_gen_start_{0};
  Subscription dest_cloud_sub_;
  Cloud::ptr dest_cloud_;

  bool Emit(IpcType type, EventKind kind = EventKind::kAck,
            std::uint32_t sequence = 0, std::uint32_t offset_ms = 0,
            std::int64_t a = 0, std::int64_t b = 0, std::int64_t c = 0) {
    IpcFrame f{};
    f.type = static_cast<std::uint8_t>(type);
    f.side = static_cast<std::uint8_t>(side);
    f.event_kind = static_cast<std::uint8_t>(kind);
    f.run_id_hash = run_id_hash;
    f.seq = ++ipc_seq;
    f.sequence = sequence;
    f.offset_ms = offset_ms;
    f.local_steady_us = SteadyUsNow();
    f.a = a;
    f.b = b;
    f.c = c;
    return pipe.WriteFrame(f);
  }

  bool EmitUdpProof(UdpProofPath path, ChannelProof const& proof) {
    IpcFrame f{};
    PackUdpProofFrame(f, path, proof);
    f.side = static_cast<std::uint8_t>(side);
    f.run_id_hash = run_id_hash;
    f.seq = ++ipc_seq;
    f.local_steady_us = SteadyUsNow();
    return pipe.WriteFrame(f);
  }

  void BeginCloudWait() {
    if (!client) {
      return;
    }
    (void)client->cloud_connection();
    waiting_cloud_link_ = true;
    cloud_wait_deadline_ = Now() + std::chrono::seconds{120};
  }

  void TryEmitOwnProof() {
    if (!client || own_proof_sent_ || no_udp_endpoint_sent_) {
      return;
    }
    auto proofs = CollectCloudConnectionProofs(client->cloud_connection());
    if (proofs.empty()) {
      if (waiting_cloud_link_ && Now() >= cloud_wait_deadline_) {
        no_udp_endpoint_sent_ = true;
        waiting_cloud_link_ = false;
        ChannelProof empty{};
        EmitUdpProof(UdpProofPath::kNoUdpEndpoint, empty);
        Emit(IpcType::kEvent, EventKind::kError, 0, 0, 30);
        exit_requested = true;
      }
      return;
    }
    own_proof_ = FirstPresentProof(proofs);
    if (!own_proof_.present) {
      return;
    }
    if (own_proof_.protocol != BenchProtocol::kUdp) {
      EmitUdpProof(UdpProofPath::kOwn, own_proof_);
      own_proof_sent_ = true;
      waiting_cloud_link_ = false;
      Emit(IpcType::kEvent, EventKind::kError, 0, 0, 31);
      exit_requested = true;
      return;
    }
    if (own_proof_.link_state != LinkState::kLinked) {
      if (waiting_cloud_link_ && Now() >= cloud_wait_deadline_) {
        no_udp_endpoint_sent_ = true;
        waiting_cloud_link_ = false;
        EmitUdpProof(UdpProofPath::kNoUdpEndpoint, own_proof_);
        Emit(IpcType::kEvent, EventKind::kError, 0, 0, 30);
        exit_requested = true;
      }
      return;
    }
    EmitUdpProof(UdpProofPath::kOwn, own_proof_);
    own_proof_sent_ = true;
    waiting_cloud_link_ = false;
    std::cerr << "UDP proof own server_id=" << own_proof_.server_id
              << " endpoint=" << own_proof_.endpoint
              << " protocol=" << BenchProtocolName(own_proof_.protocol)
              << " gen=" << own_proof_.udp_socket_generation << std::endl;
  }

  void TryEmitDestinationProof() {
    if (side != Side::kA || !client || !peer_set || dest_proof_sent_) {
      return;
    }
    ChannelProof proof{};
    if (dest_cloud_) {
      proof = CollectDestinationProofFromCloud(*client.Load(), dest_cloud_);
    }
    if (!proof.present || proof.link_state != LinkState::kLinked) {
      // Destination may share Alice's own MainServer connection.
      auto own = CollectOwnCloudProof(*client.Load());
      if (own.present && own.link_state == LinkState::kLinked) {
        proof = own;
      }
    }
    if (!proof.present || proof.link_state != LinkState::kLinked) {
      return;
    }
    dest_proof_ = proof;
    EmitUdpProof(UdpProofPath::kDestination, dest_proof_);
    dest_proof_sent_ = true;
    std::cerr << "UDP proof dest server_id=" << dest_proof_.server_id
              << " endpoint=" << dest_proof_.endpoint
              << " protocol=" << BenchProtocolName(dest_proof_.protocol)
              << " gen=" << dest_proof_.udp_socket_generation << std::endl;
    if (dest_proof_.protocol != BenchProtocol::kUdp) {
      Emit(IpcType::kEvent, EventKind::kError, 0, 0, 32);
      exit_requested = true;
    }
  }

  void StartDestinationCloudWatch() {
    if (side != Side::kA || !client || !peer_set) {
      return;
    }
    dest_cloud_sub_.Reset();
    auto& action = client->cloud_manager()->GetCloud(peer_uid);
    dest_cloud_sub_ = action.result_event().Subscribe(
        [this](Result<Cloud::ptr, int> const& res) {
          if (!res) {
            return;
          }
          dest_cloud_ = res.value();
          TryEmitDestinationProof();
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
    if (!warmup_active || side != Side::kB || !client) {
      return;
    }
    own_proof_ = CollectOwnCloudProof(*client.Load());
    if (!own_proof_.present || own_proof_.protocol != BenchProtocol::kUdp) {
      std::cerr << "Bob warmup refused: protocol="
                << BenchProtocolName(own_proof_.protocol) << std::endl;
      warmup_active = false;
      EmitUdpProof(UdpProofPath::kOwn, own_proof_);
      Emit(IpcType::kEvent, EventKind::kError, 0, 0, 33);
      exit_requested = true;
      return;
    }
    if (warmup_gen_start_ == 0) {
      warmup_gen_start_ = own_proof_.udp_socket_generation;
    }
    Duration min_rtt{};
    Duration p99_rtt{};
    auto const n = CollectResponseStats(&min_rtt, &p99_rtt);
    static std::size_t last_logged = 0;
    if (n != last_logged && (n % 2 == 0 || n >= kWarmupSamples)) {
      last_logged = n;
      std::cerr << "Bob warmup samples=" << n
                << " protocol=" << BenchProtocolName(own_proof_.protocol)
                << " gen=" << own_proof_.udp_socket_generation << std::endl;
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
    auto const gen_end = CurrentUdpSocketGeneration();
    if (gen_end < warmup_gen_start_) {
      std::cerr << "Bob warmup socket generation went backwards\n";
      warmup_active = false;
      Emit(IpcType::kEvent, EventKind::kError, 0, 0, 34);
      exit_requested = true;
      return;
    }
    warmup_active = false;
    std::cout << "## Bob ping statistics (child)\n"
              << "samples=" << n << " min_rtt_ms=" << min_ms
              << " p99_rtt_ms=" << p99_ms << " guard_ms=" << guard_ms
              << " udp_gen_start=" << warmup_gen_start_
              << " udp_gen_end=" << gen_end << "\n";
    if (!own_proof_sent_) {
      own_proof_.udp_socket_generation = gen_end;
      EmitUdpProof(UdpProofPath::kOwn, own_proof_);
      own_proof_sent_ = true;
    }
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
        [this](Result<PeerReceiveSchedule, int> const& res) {
          OnSchedule(res);
        });
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
    auto const schedule = res.value();
    auto const offset = std::chrono::milliseconds{pending_offset_ms};
    auto const target = schedule.last_ping + offset;
    auto const now = Now();
    pending_last_us_ =
        std::chrono::duration_cast<std::chrono::microseconds>(
            schedule.last_ping.time_since_epoch())
            .count();
    pending_next_us_ =
        schedule.next_ping_deadline
            ? std::chrono::duration_cast<std::chrono::microseconds>(
                  schedule.next_ping_deadline->time_since_epoch())
                  .count()
            : -1;

    auto const to_next_ms =
        schedule.next_ping_deadline
            ? std::chrono::duration_cast<std::chrono::milliseconds>(
                  *schedule.next_ping_deadline - now)
                  .count()
            : -1;
    std::cerr << "Alice schedule age_to_last_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     now - schedule.last_ping)
                     .count()
              << " to_target_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(target -
                                                                       now)
                     .count()
              << " to_next_ms=" << to_next_ms
              << " offset_ms=" << pending_offset_ms << std::endl;

    if (now + kSkipIfCloserThan > target && now < target) {
      sample_in_flight = false;
      Emit(IpcType::kSampleResult, EventKind::kSampleSkipped, pending_sequence,
           pending_offset_ms, pending_last_us_, pending_next_us_, 1);
      return;
    }
    if (now >= target) {
      // Offset already passed — wait for next Bob ping cycle, then re-query
      // outside this callback (Client replaces the action object).
      if (schedule.next_ping_deadline) {
        auto wait_until =
            *schedule.next_ping_deadline + std::chrono::milliseconds{150};
        if (wait_until <= now) {
          // Deadline already past (stale UAP); probe again shortly.
          wait_until = now + std::chrono::milliseconds{250};
        }
        if (wait_until > now + std::chrono::seconds{15}) {
          sample_in_flight = false;
          Emit(IpcType::kSampleResult, EventKind::kSampleSkipped,
               pending_sequence, pending_offset_ms, pending_last_us_,
               pending_next_us_, 2);
          return;
        }
        requery_at_ = wait_until;
        return;
      }
      sample_in_flight = false;
      Emit(IpcType::kSampleResult, EventKind::kSampleSkipped, pending_sequence,
           pending_offset_ms, pending_last_us_, pending_next_us_, 2);
      return;
    }

    auto const delay_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(target - now)
            .count();
    if (delay_ms > 15000) {
      sample_in_flight = false;
      Emit(IpcType::kSampleResult, EventKind::kSampleSkipped, pending_sequence,
           pending_offset_ms, pending_last_us_, pending_next_us_, 3);
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

    own_proof_ = CollectOwnCloudProof(*client.Load());
    TryEmitDestinationProof();
    auto const own_proto = own_proof_.present ? own_proof_.protocol
                                              : BenchProtocol::kUnknown;
    auto const dest_proto = dest_proof_.present ? dest_proof_.protocol
                                                : own_proto;
    if (RefuseTcpSample(own_proto, dest_proto) ||
        own_proto != BenchProtocol::kUdp ||
        dest_proto != BenchProtocol::kUdp) {
      sample_in_flight = false;
      std::cerr << "Alice refuse TCP/non-UDP sample own="
                << BenchProtocolName(own_proto)
                << " dest=" << BenchProtocolName(dest_proto) << std::endl;
      Emit(IpcType::kSampleResult, EventKind::kError, pending_sequence,
           pending_offset_ms, 35);
      return;
    }

    DeliveryBenchMessage msg{};
    msg.offset_ms = static_cast<std::uint16_t>(pending_offset_ms);
    msg.sequence = pending_sequence;
    msg.send_qpc = QpcNow();
    auto bytes = SerializeDeliveryBenchMessage(msg);
    DataBuffer payload{bytes.begin(), bytes.end()};
    stream->Write(std::move(payload));
    sample_in_flight = false;
    Emit(IpcType::kSampleResult, EventKind::kSampleSent, pending_sequence,
         pending_offset_ms, pending_last_us_, pending_next_us_,
         static_cast<std::int64_t>(msg.send_qpc));
    std::cerr << "Alice sent seq=" << pending_sequence
              << " offset_ms=" << pending_offset_ms << std::endl;
  }

  void HandleIpc(IpcFrame const& f) {
    auto const type = static_cast<IpcType>(f.type);
    switch (type) {
      case IpcType::kSetPeerUid:
        peer_uid = UidFromHalves(f.a, f.b);
        peer_set = true;
        if (client) {
          (void)client->cloud_connection();
        }
        EnsureStreams();
        StartDestinationCloudWatch();
        Emit(IpcType::kAck, EventKind::kAck);
        break;
      case IpcType::kWaitWarmup:
        if (side == Side::kB) {
          warmup_active = true;
          warmup_gen_start_ = CurrentUdpSocketGeneration();
          std::cerr << "Bob WaitWarmup received gen_start=" << warmup_gen_start_
                    << std::endl;
          if (client) {
            (void)client->cloud_connection();
          }
        }
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

bool RegistrationCloudHasUdpEndpoint(Aether& aether) {
#if AE_SUPPORT_REGISTRATION
  // RegistrationCloudFactory hardcodes Protocol::kTcp endpoints only.
  // With AE_SUPPORT_TCP=0 those endpoints are filtered out of channels.
  (void)aether;
  return false;
#else
  (void)aether;
  return true;
#endif
}

}  // namespace

int RunClientRole(ClientArgs const& args) {
  // Unbuffered child logs (coordinator redirects stdout/stderr to files).
  setvbuf(stdout, nullptr, _IONBF, 0);
  setvbuf(stderr, nullptr, _IONBF, 0);

  RoleState state;
  state.side = args.side;
  state.run_id_hash = HashRunId(args.run_id);

  std::cerr << "Client role start side="
            << (args.side == Side::kA ? "A" : "B")
            << " state_dir=" << args.state_dir
            << " AE_SUPPORT_TCP=" << AE_SUPPORT_TCP
            << " AE_SUPPORT_UDP=" << AE_SUPPORT_UDP << std::endl;

  if (!state.pipe.Connect(args.pipe_name, 60000)) {
    std::cerr << "pipe connect failed: " << args.pipe_name << "\n";
    return 2;
  }
  std::cerr << "pipe connected\n";

  state.app = MakeApp(args.state_dir);
  std::cerr << "AetherApp constructed; SelectClient...\n";
  auto parent = Uid::FromString(args.parent_uid);
  auto& aether = *state.app->aether().Load();
  auto const reg_has_udp = RegistrationCloudHasUdpEndpoint(aether);
  std::cerr << "registration_cloud_has_udp=" << (reg_has_udp ? 1 : 0)
            << std::endl;
  auto& select = aether.SelectClient(parent, args.client_name);
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
            std::cerr << "Bob SetReceiveSchedule soft-fail err=" << ok.error()
                      << " continuing with persisted policy if any\n";
          }
        }
        state.client_ready = true;
        state.BeginCloudWait();
        std::int64_t lo = 0;
        std::int64_t hi = 0;
        UidToHalves(state.client->uid(), lo, hi);
        std::cerr << "Client ready side="
                  << (state.side == Side::kA ? "A" : "B") << std::endl;
        state.Emit(IpcType::kUidReport, EventKind::kChildReady, 0, 0, lo, hi);
        state.Emit(IpcType::kChildReady, EventKind::kChildReady);
      });

  auto const select_deadline = Now() + std::chrono::seconds{45};
  while (!state.exit_requested && !state.app->IsExited()) {
    auto const now = Now();
    auto next = state.app->Update(now);
    if (auto frame = state.pipe.TryReadFrame(0)) {
      state.HandleIpc(*frame);
    }
    if (!state.client_ready && !reg_has_udp && now >= select_deadline) {
      std::cerr << "blocker: server_does_not_advertise_udp_endpoint "
                   "(SelectClient timed out; registration cloud is TCP-only "
                   "under AE_SUPPORT_TCP=0)\n";
      ChannelProof empty{};
      state.EmitUdpProof(UdpProofPath::kNoUdpEndpoint, empty);
      state.Emit(IpcType::kEvent, EventKind::kError, 0, 0, 30);
      return 30;
    }
    state.TryEmitOwnProof();
    state.TryEmitDestinationProof();
    state.PollWarmup();
    state.PollSampleTiming();
    state.app->WaitUntil(
        std::min(next, now + std::chrono::milliseconds{5}));
  }
  return 0;
}

}  // namespace ae::bench::uap
