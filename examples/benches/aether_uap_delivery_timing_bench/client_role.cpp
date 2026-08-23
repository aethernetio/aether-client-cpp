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
    if (side != Side::kA || !client || !peer_set || sample_in_flight) {
      return;
    }
    sample_in_flight = true;
    pending_sequence = sequence;
    pending_offset_ms = offset_ms;
    EnsureStreams();
    // Client owns the action; each call replaces the previous instance.
    auto& action = client->QueryPeerReceiveSchedule(peer_uid);
    query_sub = action.result_event().Subscribe(
        [this](Result<PeerReceiveSchedule, int> const& res) {
          OnSchedule(res);
        });
  }

  void RequerySchedule() {
    auto& action = client->QueryPeerReceiveSchedule(peer_uid);
    query_sub = action.result_event().Subscribe(
        [this](Result<PeerReceiveSchedule, int> const& r) { OnSchedule(r); });
  }

  void OnSchedule(Result<PeerReceiveSchedule, int> const& res) {
    if (!res) {
      sample_in_flight = false;
      Emit(IpcType::kSampleResult, EventKind::kError, pending_sequence,
           pending_offset_ms, res.error());
      return;
    }
    auto const schedule = res.value();
    auto const offset = std::chrono::milliseconds{pending_offset_ms};
    auto const target = schedule.last_ping + offset;
    // PeerReceiveSchedule uses ae::TimePoint (system clock), matching Now().
    auto const now = Now();
    auto const last_us =
        std::chrono::duration_cast<std::chrono::microseconds>(
            schedule.last_ping.time_since_epoch())
            .count();
    auto const next_us =
        schedule.next_ping_deadline
            ? std::chrono::duration_cast<std::chrono::microseconds>(
                  schedule.next_ping_deadline->time_since_epoch())
                  .count()
            : -1;

    if (now + kSkipIfCloserThan > target && now < target) {
      sample_in_flight = false;
      Emit(IpcType::kSampleResult, EventKind::kSampleSkipped, pending_sequence,
           pending_offset_ms, last_us, next_us, 1);
      return;
    }
    if (now >= target) {
      // Offset already passed this cycle — wait for next ping then re-query.
      if (schedule.next_ping_deadline &&
          now < *schedule.next_ping_deadline + std::chrono::milliseconds{200}) {
        auto const wait_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                *schedule.next_ping_deadline - now +
                std::chrono::milliseconds{100})
                .count();
        if (wait_ms > 0) {
          WaitMsPrecise(wait_ms);
        }
        RequerySchedule();
        return;
      }
      sample_in_flight = false;
      Emit(IpcType::kSampleResult, EventKind::kSampleSkipped, pending_sequence,
           pending_offset_ms, last_us, next_us, 2);
      return;
    }

    auto const delay_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(target - now)
            .count();
    WaitMsPrecise(delay_ms);

    if (!stream) {
      EnsureStreams();
    }
    if (!stream) {
      sample_in_flight = false;
      Emit(IpcType::kSampleResult, EventKind::kError, pending_sequence,
           pending_offset_ms, 3);
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
         pending_offset_ms, last_us, next_us,
         static_cast<std::int64_t>(msg.send_qpc));
  }

  void HandleIpc(IpcFrame const& f) {
    auto const type = static_cast<IpcType>(f.type);
    switch (type) {
      case IpcType::kSetPeerUid:
        peer_uid = UidFromHalves(f.a, f.b);
        peer_set = true;
        if (client) {
          // Bob: schedule already applied; Alice: default schedule.
          (void)client->cloud_connection();
        }
        EnsureStreams();
        Emit(IpcType::kAck, EventKind::kAck);
        break;
      case IpcType::kWaitWarmup:
        if (side == Side::kB) {
          warmup_active = true;
          std::cerr << "Bob WaitWarmup received" << std::endl;
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
    state.PollWarmup();
    state.app->WaitUntil(
        std::min(next, now + std::chrono::milliseconds{5}));
  }
  return 0;
}

}  // namespace ae::bench::uap
