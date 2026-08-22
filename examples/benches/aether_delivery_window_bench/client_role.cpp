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
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "aether/all.h"
#include "aether/ae_actions/query_peer_ping_schedule.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/cloud_connections/cloud_request.h"
#include "aether/cloud_connections/ping_bench_hooks.h"
#include "aether/work_cloud_api/ae_message.h"
#include "aether/work_cloud_api/work_server_api/authorized_api.h"

#include "common/bench_ipc.h"
#include "common/bench_message.h"
#include "common/directory_domain_storage.h"
#include "common/ring_trace.h"

namespace ae::bench::dw {
namespace {

struct ClientRoleState;
thread_local ClientRoleState* g_client_state{nullptr};

struct ClientRoleState {
  Side side{};
  std::string run_id;
  std::uint32_t run_id_hash{0};
  NamedPipeClient pipe;
  std::unique_ptr<RingTrace<>> trace{std::make_unique<RingTrace<>>()};
  std::mutex mu;
  std::unique_ptr<AetherApp> app;
  Client::ptr client;
  Uid peer_uid{};
  bool peer_set{false};
  std::shared_ptr<P2pStream> stream;
  std::unique_ptr<CloudServerConnections> dest_cloud;
  Cloud::ptr peer_cloud;
  Subscription stream_sub;
  Subscription new_port_sub;
  Subscription send_promise_sub;
  Subscription get_cloud_sub;
  Subscription cloud_req_sub;
  std::unique_ptr<CloudRequest> cloud_req;
  std::unique_ptr<QueryPeerPingSchedule> uap_query;
  Subscription uap_sub;
  std::unordered_map<std::uint32_t, int> seen_message_ids;
  int warmup_window_opens{0};
  bool exit_requested{false};
  std::string trace_path;
  std::uint32_t seq{0};

  // Pending send kept out of SmallFunction captures.
  AeMessage pending_ae_msg{};
  std::uint32_t pending_config_id{0};
  std::uint32_t pending_cycle_id{0};
  std::uint32_t pending_message_id{0};
  Direction pending_dir{Direction::kAtoB};
  std::vector<std::uint8_t> pending_bytes;

  std::int64_t NowUs() const {
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  void TraceLocal(EventKind kind, std::uint32_t cycle_id,
                  std::uint32_t message_id, std::uint16_t server_id,
                  std::int64_t a = 0, std::int64_t b = 0,
                  std::int64_t c = 0) {
    TraceEntry e;
    e.local_steady_us = NowUs();
    e.cycle_id = cycle_id;
    e.message_id = message_id;
    e.server_id = server_id;
    e.event_kind = static_cast<std::uint8_t>(kind);
    e.a = a;
    e.b = b;
    e.c = c;
    std::lock_guard lock{mu};
    trace->Push(e);
  }

  bool EmitIpc(IpcType type, EventKind kind = EventKind::kAck,
               std::uint32_t cycle_id = 0, std::uint32_t message_id = 0,
               std::uint16_t server_id = 0, std::int64_t a = 0,
               std::int64_t b = 0, std::int64_t c = 0,
               std::uint32_t config_id = 0, Direction dir = Direction::kAtoB) {
    IpcFrame f{};
    f.type = static_cast<std::uint8_t>(type);
    f.side = static_cast<std::uint8_t>(side);
    f.event_kind = static_cast<std::uint8_t>(kind);
    f.run_id_hash = run_id_hash;
    f.seq = ++seq;
    f.config_id = config_id;
    f.cycle_id = cycle_id;
    f.message_id = message_id;
    f.server_id = server_id;
    f.direction = static_cast<std::uint8_t>(dir);
    f.local_steady_us = NowUs();
    f.a = a;
    f.b = b;
    f.c = c;
    TraceLocal(kind, cycle_id, message_id, server_id, a, b, c);
    return pipe.WriteFrame(f);
  }

  void OnPingBench(PingBenchEvent const& ev) {
    auto kind = EventKind::kPingScheduled;
    switch (ev.kind) {
      case PingBenchEventKind::kPingScheduled:
        kind = EventKind::kPingScheduled;
        break;
      case PingBenchEventKind::kPingWriteBegin:
        kind = EventKind::kPingWriteBegin;
        break;
      case PingBenchEventKind::kPingServerAck:
        kind = EventKind::kPingServerAck;
        break;
      case PingBenchEventKind::kRxWindowOpen:
        kind = EventKind::kRxWindowOpen;
        ++warmup_window_opens;
        break;
      case PingBenchEventKind::kRxWindowClose:
        kind = EventKind::kRxWindowClose;
        break;
      case PingBenchEventKind::kNextPingScheduled:
        kind = EventKind::kNextPingScheduled;
        break;
    }
    EmitIpc(IpcType::kEvent, kind, ev.cycle_index, 0, ev.server_id,
            ev.actual_ping_interval_ms, ev.announced_next_ping_ms,
            ev.rx_window_ms);
  }

  void EnsureReceiveStream() {
    if (!client || !peer_set) {
      return;
    }
    if (!stream) {
      stream = std::make_shared<P2pStream>(
          AeContext{*app}, client.Load(), peer_uid,
          client->message_stream_manager().CreatePort(peer_uid));
      stream_sub = stream->out_data_event().Subscribe(
          [this](DataBuffer const& data) { OnReceive(data); });
    }
    new_port_sub =
        client->message_stream_manager().new_port_event().Subscribe(
            [this](P2pPortHandle handle) {
              if (handle.destination() != peer_uid) {
                return;
              }
              stream = std::make_shared<P2pStream>(
                  AeContext{*app}, client.Load(), handle.destination(),
                  std::move(handle));
              stream_sub = stream->out_data_event().Subscribe(
                  [this](DataBuffer const& data) { OnReceive(data); });
            });
  }

  void OnReceive(DataBuffer const& data) {
    auto msg = DeserializeBenchMessage(data.data(), data.size());
    if (!msg) {
      return;
    }
    auto& count = seen_message_ids[msg->message_id];
    ++count;
    EmitIpc(IpcType::kEvent, EventKind::kMessageReceived, msg->cycle_id,
            msg->message_id, 0, count, msg->config_id, 0, msg->config_id,
            static_cast<Direction>(msg->direction));
  }

  void PrefetchPeerCloud() {
    if (!client || !peer_set) {
      return;
    }
    auto& get_cloud = client->cloud_manager()->GetCloud(peer_uid);
    get_cloud_sub = get_cloud.result_event().Subscribe(
        [this](Result<Cloud::ptr, int>&& result) {
          if (!result) {
            EmitIpc(IpcType::kEvent, EventKind::kError, 0, 0, 0, 2);
            return;
          }
          peer_cloud = result.value();
          EmitIpc(IpcType::kAck, EventKind::kAck, 0, 0, 0, 100);
        });
  }

  void BeginSendWithCloud(Cloud::ptr const& cloud) {
    dest_cloud = std::make_unique<CloudServerConnections>(
        AeContext{*app}, cloud.Load(),
        client->server_connection_manager().GetServerConnectionFactory(),
        AE_CLOUD_MAX_SERVER_CONNECTIONS);

    pending_ae_msg.uid = peer_uid;
    pending_ae_msg.data.assign(pending_bytes.begin(), pending_bytes.end());

    EmitIpc(IpcType::kEvent, EventKind::kMessageSendCall, pending_cycle_id,
            pending_message_id, 0, 0, 0, 0, pending_config_id, pending_dir);

    cloud_req = std::make_unique<CloudRequest>(
        AeContext{*app},
        ApiRequestHandler{MethodPtr<&ClientRoleState::OnSendApiRequest>{this}},
        *dest_cloud, RequestPolicy::MainServer{});

    cloud_req_sub = cloud_req->result_event().Subscribe(
        [this](bool ok) {
          if (!ok) {
            EmitIpc(IpcType::kEvent, EventKind::kError, pending_cycle_id,
                    pending_message_id, 0, 4);
          }
        });
  }

  void OnCloudForSend(Result<Cloud::ptr, int>&& result) {
    if (!result) {
      EmitIpc(IpcType::kEvent, EventKind::kError, pending_cycle_id,
              pending_message_id, 0, 2);
      return;
    }
    peer_cloud = result.value();
    BeginSendWithCloud(peer_cloud);
  }

  void OnSendApiRequest(ApiContext<AuthorizedApi>& auth_api,
                        CloudServerConnection* sc, CloudRequest* request) {
    auto server_id =
        sc && sc->server() ? sc->server()->server_id : ServerId{};
    // Copy: CloudRequest may retry the handler after FailAttempt.
    AeMessage msg = pending_ae_msg;
    auto promise = auth_api->send_message_with_result(std::move(msg));
    send_promise_sub = promise.Subscribe(
        [this, request, server_id](auto const& res) {
          if (!res) {
            EmitIpc(IpcType::kEvent, EventKind::kError, pending_cycle_id,
                    pending_message_id, server_id, 3);
            if (request) {
              request->FailAttempt(nullptr);
            }
            return;
          }
          EmitIpc(IpcType::kEvent, EventKind::kMessageServerAccepted,
                  pending_cycle_id, pending_message_id, server_id, 0, 0, 0,
                  pending_config_id, pending_dir);
          if (request) {
            request->Succeeded();
          }
        });
  }

  void SendBenchMessage(std::uint32_t config_id, std::uint32_t cycle_id,
                        std::uint32_t message_id, Direction dir) {
    if (!client || !peer_set) {
      EmitIpc(IpcType::kEvent, EventKind::kError, cycle_id, message_id, 0, 1);
      return;
    }
    EnsureReceiveStream();

    BenchMessage payload{};
    payload.run_id_hash = run_id_hash;
    payload.config_id = config_id;
    payload.cycle_id = cycle_id;
    payload.message_id = message_id;
    payload.direction = static_cast<std::uint8_t>(dir);
    pending_bytes = SerializeBenchMessage(payload);
    pending_config_id = config_id;
    pending_cycle_id = cycle_id;
    pending_message_id = message_id;
    pending_dir = dir;

    EmitIpc(IpcType::kEvent, EventKind::kMessageSendCommand, cycle_id,
            message_id, 0, 0, 0, 0, config_id, dir);

    if (peer_cloud) {
      BeginSendWithCloud(peer_cloud);
      return;
    }

    auto& get_cloud = client->cloud_manager()->GetCloud(peer_uid);
    get_cloud_sub = get_cloud.result_event().Subscribe(
        [this](Result<Cloud::ptr, int>&& result) {
          OnCloudForSend(std::move(result));
        });
  }

  void PullMessages(std::uint32_t cycle_id, std::uint32_t message_id) {
    if (!client) {
      return;
    }
    EmitIpc(IpcType::kEvent, EventKind::kPullRequest, cycle_id, message_id);
    client->cloud_connection().CallApi(
        ApiCall{[](ApiContext<AuthorizedApi>& auth_api, auto*) {
          auth_api->pull_messages();
        }},
        RequestPolicy::MainServer{});
  }

  void RunUapVerify(std::uint32_t config_id) {
    if (!client || !peer_set) {
      EmitIpc(IpcType::kUapResult, EventKind::kError, 0, 0, 0, -1, 0, 0,
              config_id);
      return;
    }
    uap_query = std::make_unique<QueryPeerPingSchedule>(
        AeContext{*app}, *client.Load(), client->uid());
    uap_sub = uap_query->result_event().Subscribe(
        [this, config_id](auto const& res) {
          if (!res) {
            EmitIpc(IpcType::kUapResult, EventKind::kError, 0, 0, 0,
                    static_cast<std::int64_t>(res.error()), 0, 0, config_id);
            return;
          }
          auto const& s = res.value();
          EmitIpc(IpcType::kEvent, EventKind::kUapServerNow, 0, 0,
                  s.server_id, s.server_now_ms);
          EmitIpc(IpcType::kEvent, EventKind::kUapLastRead, 0, 0, s.server_id,
                  s.last_ping_server_ms);
          EmitIpc(IpcType::kEvent, EventKind::kUapDelta, 0, 0, s.server_id,
                  s.next_ping_delta_ms);
          EmitIpc(IpcType::kEvent, EventKind::kUapExpectedNext, 0, 0,
                  s.server_id,
                  s.last_ping_server_ms + s.next_ping_delta_ms);
          EmitIpc(IpcType::kUapResult, EventKind::kAck, 0, 0, s.server_id,
                  s.server_now_ms, s.last_ping_server_ms, s.next_ping_delta_ms,
                  config_id);
        });
  }

  void HandleFrame(IpcFrame const& f) {
    auto const type = static_cast<IpcType>(f.type);
    switch (type) {
      case IpcType::kCalibPing: {
        IpcFrame pong = f;
        pong.type = static_cast<std::uint8_t>(IpcType::kCalibPong);
        pong.side = static_cast<std::uint8_t>(side);
        pong.local_steady_us = NowUs();
        pipe.WriteFrame(pong);
        break;
      }
      case IpcType::kSetPeerUid: {
        // peer uid encoded as hex in a/b/c not available — use message payload
        // Coordinator sends peer uid bytes in a,b via separate string command.
        break;
      }
      case IpcType::kWaitWarmupPings: {
        warmup_window_opens = 0;
        // Completion checked in Update loop.
        break;
      }
      case IpcType::kUapVerify:
        RunUapVerify(f.config_id);
        break;
      case IpcType::kWarmupMessage:
        SendBenchMessage(f.config_id, f.cycle_id, f.message_id,
                         static_cast<Direction>(f.direction));
        break;
      case IpcType::kSendMessage:
        SendBenchMessage(f.config_id, f.cycle_id, f.message_id,
                         static_cast<Direction>(f.direction));
        break;
      case IpcType::kPullMessages:
        PullMessages(f.cycle_id, f.message_id);
        break;
      case IpcType::kFlushTrace:
        trace->FlushCsv(trace_path);
        EmitIpc(IpcType::kAck, EventKind::kAck);
        break;
      case IpcType::kShutdown:
        exit_requested = true;
        if (app) {
          app->Exit(0);
        }
        break;
      default:
        break;
    }
  }
};

void PingObserverThunk(PingBenchEvent const& event) noexcept {
  if (g_client_state != nullptr) {
    g_client_state->OnPingBench(event);
  }
}

std::unique_ptr<AetherApp> ConstructApp(std::filesystem::path state_dir) {
  auto root = state_dir;
  return AetherApp::Construct(AetherAppContext{[root]() {
                                return std::make_unique<DirectoryDomainStorage>(
                                    root);
                              }}
#if AE_DISTILLATION
                                  .AddAdapterFactory(
                                      [](AetherAppContext const& context) {
                                        return EthernetAdapter::ptr::Create(
                                            CreateWith{context.domain()}.with_id(
                                                GlobalId::kEthernetAdapter),
                                            context.aether(), context.poller(),
                                            context.dns_resolver());
                                      })
#endif
  );
}

}  // namespace

int RunClientRole(ClientArgs const& args) {
#if !defined(_WIN32)
  (void)args;
  return 2;
#else
  auto state_ptr = std::make_unique<ClientRoleState>();
  auto& state = *state_ptr;
  state.side = args.side;
  state.run_id = args.run_id;
  state.run_id_hash = HashRunId(args.run_id);
  state.trace_path = args.trace_path;
  g_client_state = &state;

  if (args.has_timing) {
    SetPingTimingOverride(PingTimingOverride{
        std::chrono::milliseconds{args.timing.actual_ping_interval_ms},
        std::chrono::milliseconds{args.timing.announced_next_ping_ms},
        std::chrono::milliseconds{args.timing.rx_window_ms},
    });
  }
  SetPingBenchObserver(&PingObserverThunk);

  std::filesystem::create_directories(args.state_dir);
  state.app = ConstructApp(args.state_dir);
  if (!state.app) {
    return 3;
  }

  if (!state.pipe.Connect(args.pipe_name, 30000)) {
    return 4;
  }

  auto parent = Uid::FromString(args.parent_uid);
  auto& select =
      state.app->aether()->SelectClient(parent, args.client_name);
  select.result_event().Subscribe([&](auto const& res) {
    if (!res) {
      state.app->Exit(5);
      return;
    }
    state.client = res.value();
    auto const& u = state.client->uid();
    std::uint64_t hi = 0;
    std::uint64_t lo = 0;
    std::memcpy(&lo, u.value.data(), 8);
    std::memcpy(&hi, u.value.data() + 8, 8);
    std::int64_t lo_i = 0;
    std::int64_t hi_i = 0;
    std::memcpy(&lo_i, &lo, 8);
    std::memcpy(&hi_i, &hi, 8);
    state.EmitIpc(IpcType::kUidReport, EventKind::kChildReady, 0, 0, 0, lo_i,
                  hi_i);
    state.EmitIpc(IpcType::kChildReady, EventKind::kChildReady);
  });

  // Peer uid may arrive as hex string via env AE_BENCH_PEER_UID or args.
  if (!args.peer_uid.empty()) {
    state.peer_uid = Uid::FromString(args.peer_uid);
    state.peer_set = true;
    state.EnsureReceiveStream();
  }

  int warmup_target = 0;
  bool waiting_warmup = false;

  while (!state.app->IsExited()) {
    auto time = TimePoint::clock::now();
    auto next = state.app->Update(time);

    if (auto frame = state.pipe.TryReadFrame(0)) {
      if (static_cast<IpcType>(frame->type) == IpcType::kWaitWarmupPings) {
        waiting_warmup = true;
        warmup_target = static_cast<int>(frame->a);
        if (warmup_target <= 0) {
          warmup_target = 2;
        }
        state.warmup_window_opens = 0;
      } else if (static_cast<IpcType>(frame->type) == IpcType::kSetPeerUid) {
        Uid uid{};
        auto lo = static_cast<std::uint64_t>(frame->a);
        auto hi = static_cast<std::uint64_t>(frame->b);
        std::memcpy(uid.value.data(), &lo, 8);
        std::memcpy(uid.value.data() + 8, &hi, 8);
        state.peer_uid = uid;
        state.peer_set = true;
        state.EnsureReceiveStream();
        state.PrefetchPeerCloud();
        state.EmitIpc(IpcType::kAck, EventKind::kAck);
      } else {
        state.HandleFrame(*frame);
      }
    }

    if (waiting_warmup && state.warmup_window_opens >= warmup_target) {
      waiting_warmup = false;
      state.EmitIpc(IpcType::kWarmupPingsDone, EventKind::kWarmupDone);
    }

    state.app->WaitUntil(
        (std::min)(next, time + std::chrono::milliseconds(5)));
  }

  state.trace->FlushCsv(state.trace_path);
  SetPingBenchObserver(nullptr);
  SetPingTimingOverride(std::nullopt);
  g_client_state = nullptr;
  return state.app->ExitCode();
#endif
}

}  // namespace ae::bench::dw
