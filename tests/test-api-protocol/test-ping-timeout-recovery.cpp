/*
 * Copyright 2026 Aethernet Inc.
 *
 * Integration tests: ping timeout soft quarantine, late Pong recovery, expiry.
 */

#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

#include <unity.h>

#include "aether/tasks/details/task_subsctiption.h"

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "aether/adapter_registry.h"
#include "aether/aether.h"
#include "aether/api_protocol/api_protocol.h"
#include "aether/channels/channel.h"
#include "aether/client.h"
#include "aether/client_connectivity_policy.h"
#include "aether/client_messages/p2p_message_stream_manager.h"
#include "aether/cloud.h"
#include "aether/cloud_connections/cloud_server_connection.h"
#include "aether/cloud_connections/cloud_server_connections.h"
#include "aether/cloud_connections/ping_cloud_servers.h"
#include "aether/config.h"
#include "aether/crypto/key_gen.h"
#include "aether/crypto/sync_crypto_provider.h"
#include "aether/env.h"
#include "aether/executors/executors.h"
#include "aether/server.h"
#include "aether/server_connections/client_server_connection.h"
#include "aether/server_connections/iserver_connection_factory.h"
#include "aether/socket_initializer.h"
#include "aether/stream_api/istream.h"
#include "aether/types/address.h"
#include "aether/work_cloud_api/ae_message.h"
#include "aether/write_action/write_action.h"

#include "ping_cloud_servers_test_access.h"

#if AE_ENABLE_PING
namespace ae {
struct P2pMessageStreamManagerTestAccess {
  static bool HasSubscriptions(P2pMessageStreamManager const& manager) {
    return static_cast<bool>(manager.on_message_received_sub_.subscriptions_);
  }
};

struct CloudServerConnectionsTestAccess {
  static bool HasErrorSubscription(CloudServerConnections& connections,
                                   CloudServerConnection& server) {
    return static_cast<bool>(connections.ServerEntryFor(server).error_sub);
  }

  static bool QuarantinePingTimeout(CloudServerConnections& connections,
                                    CloudServerConnection& server) {
    return connections.QuarantineForPingTimeout(server);
  }

  static void ScheduleReconcile(CloudServerConnections& connections) {
    connections.ScheduleReconcileServers();
  }

  static CloudServerConnection* Main(CloudServerConnections& connections) {
    if (connections.selected_servers().empty()) {
      return nullptr;
    }
    return connections.selected_servers().front();
  }

  static bool RecoverPingTimeout(CloudServerConnections& connections,
                                 CloudServerConnection& server) {
    return connections.RecoverFromPingTimeout(server);
  }

  static bool SubscribeServerState(CloudServerConnections& connections,
                                   CloudServerConnection& server) {
    return connections.SubscribeToServerState(server);
  }

  static void EmitServersUpdate(CloudServerConnections& connections) {
    connections.servers_update_event_.Emit();
  }
};
}  // namespace ae

namespace ae::test_ping_timeout_recovery {
namespace {

SocketInitializer g_socket_initializer;

constexpr ServerId kServerA{42};
constexpr ServerId kServerB{43};
constexpr std::size_t kMaxSelectedServers = 1;
constexpr auto kPingAckMessageId = MessageId{24};
constexpr auto kSendSafeApiMessageId = MessageId{4};

struct TestContext : public AeEnv {
  void Advance(Duration delta) {
    auto const end = test_now_ + delta;
    while (test_now_ < end) {
      test_now_ += std::chrono::milliseconds{1};
      scheduler().Update(test_now_);
    }
  }

  TimePoint test_now_{std::chrono::system_clock::now()};
};

class TrackedMockStream final : public ByteIStream {
 public:
  explicit TrackedMockStream(AeContext const& context, ServerId id)
      : context_{context},
        server_id_{id},
        stream_info_{512, 1024, true, LinkState::kLinked, true},
        out_data_event_{context_},
        stream_update_event_{context_} {}

  WriteAction& Write(DataBuffer&& data) override {
    ++write_count_;
    writes_.push_back(std::move(data));
    return write_;
  }
  StreamUpdateEvent const& stream_update_event() override {
    return stream_update_event_;
  }
  StreamInfo stream_info() const override { return stream_info_; }
  OutDataEvent const& out_data_event() override { return out_data_event_; }
  void Restream() override { ++restream_count_; }

  void Deliver(DataBuffer data) { out_data_event_.Emit(std::move(data)); }

  ServerId server_id() const { return server_id_; }
  int write_count() const { return write_count_; }
  int restream_count() const { return restream_count_; }
  auto const& writes() const { return writes_; }

 private:
  class OkWrite final : public WriteAction {
   public:
    explicit OkWrite(AeContext const& c) : WriteAction{c} {
      completion_sub_ =
          c.scheduler().Task([this] { SetStatus(Status::kSuccess); });
    }

   private:
    TaskSubscription completion_sub_;
  };

  AeContext context_;
  ServerId server_id_;
  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;
  OkWrite write_{context_};
  int write_count_{0};
  int restream_count_{0};
  std::vector<DataBuffer> writes_;
};

struct ChannelPolicy {
  AeContext const* ctx{nullptr};
  ServerId server_id{};
  std::map<ServerId, TrackedMockStream*>* streams{nullptr};
};

class FakeChannel final : public Channel {
  AE_OBJECT(FakeChannel, Channel, 5)

 protected:
  FakeChannel() = default;

 public:
  FakeChannel(ObjProp prop, ChannelPolicy policy)
      : Channel{prop}, policy_{policy} {
    transport_properties_.max_packet_size = 1024;
    transport_properties_.rec_packet_size = 512;
    transport_properties_.connection_type = ConnectionType::kConnectionFull;
    transport_properties_.reliability = Reliability::kReliable;
  }

  AE_OBJECT_REFLECT()

  TransportBuildSender TransportBuilder() override {
    auto stream =
        std::make_unique<TrackedMockStream>(*policy_.ctx, policy_.server_id);
    if (policy_.streams != nullptr) {
      (*policy_.streams)[policy_.server_id] = stream.get();
    }
    return ex::just(std::unique_ptr<ByteIStream>{std::move(stream)});
  }

  Duration TransportBuildTimeout() const override {
    return std::chrono::milliseconds{50};
  }
  Duration ResponseTimeout() const override {
    return std::chrono::milliseconds{50};
  }
  std::optional<Endpoint> endpoint() const override { return std::nullopt; }

 private:
  ChannelPolicy policy_;
};

class Factory final : public IServerConnectionFactory {
 public:
  Factory(AeContext ctx, ObjPtr<Client> client, bool retain_connections = false)
      : ctx_{ctx}, client_{client}, retain_connections_{retain_connections} {}
  std::shared_ptr<ClientServerConnection> CreateConnection(
      Ptr<Server> const& server) override {
    auto connection =
        std::make_shared<ClientServerConnection>(ctx_, client_.Load(), server);
    if (retain_connections_) {
      retained_connections_.push_back(connection);
    }
    return connection;
  }

 private:
  AeContext ctx_;
  ObjPtr<Client> client_;
  bool retain_connections_;
  std::vector<std::shared_ptr<ClientServerConnection>> retained_connections_;
};

class InboundKeyProvider final : public ISyncKeyProvider {
 public:
  InboundKeyProvider(ObjPtr<Client> client, ServerId server_id,
                     bool client_to_server = false)
      : client_{client},
        server_id_{server_id},
        client_to_server_{client_to_server} {}
  Key GetKey() const override {
    auto* state = client_.Load()->server_state(server_id_);
    return client_to_server_ ? state->client_to_server()
                             : state->server_to_client();
  }
  CryptoNonce const& Nonce() const override {
    auto* st = client_.Load()->server_state(server_id_);
    st->Next();
    return st->nonce();
  }

 private:
  ObjPtr<Client> client_;
  ServerId server_id_;
  bool client_to_server_;
};

DataBuffer PingAckPlaintext() {
  DataBuffer packet;
  ApiPacker packer{packet};
  packer.Pack(kPingAckMessageId, GenericMessage<>{});
  return packet;
}

DataBuffer PackEncryptedSafeApi(ObjPtr<Client> const& client, ServerId id,
                                DataBuffer const& inner) {
  SyncEncryptProvider enc{std::make_unique<InboundKeyProvider>(client, id)};
  DataBuffer outer;
  ApiPacker packer{outer};
  packer.Pack(kSendSafeApiMessageId, enc.Encrypt(inner));
  return outer;
}

struct Env {
  TestContext ctx;
  RamDomainStorage storage;
  AeContext ae_ctx;
  std::unique_ptr<Domain> domain;
  ObjPtr<Client> client;
  std::map<ServerId, TrackedMockStream*> streams;
  std::unique_ptr<Factory> factory;
  std::optional<CloudServerConnections> connections;
  std::unique_ptr<PingCloudServers> ping_cloud;

  Env() : ae_ctx{ctx}, domain{std::make_unique<Domain>(storage, &ctx)} {}
};

std::unique_ptr<Env> MakeEnv(bool start_ping_cloud = true,
                             std::size_t max_selected = kMaxSelectedServers,
                             bool retain_connections = false) {
  auto env = std::make_unique<Env>();
  Env& e = *env;

  auto aether = Aether::ptr::Create(CreateWith{*e.domain});
  auto registry = AdapterRegistry::ptr::Create(CreateWith{*e.domain});
  aether->adapter_registry = registry;
  e.ctx.SetAether(aether);
  aether->client_prefab = Client::ptr::Create(CreateWith{*e.domain}, aether);
  aether->client_prefab.Save();

  Key master_key{};
  if (!CryptoSyncKeygen(master_key)) {
    return nullptr;
  }
  Endpoint ep{{IpV4Addr{{192, 0, 2, 1}}, 9021}, Protocol::kUdp};
  ClientConfig config{.parent_uid = Uid{{1}},
                      .uid = Uid{{2}},
                      .ephemeral_uid = Uid{{3}},
                      .master_key = std::move(master_key),
                      .cloud = {{kServerA, {ep}}, {kServerB, {ep}}}};
  e.client = aether->CreateClient(config, "ping-timeout-recovery");

  for (auto sid : {kServerA, kServerB}) {
    auto server = aether->GetServer(sid).Load();
    ChannelPolicy pol{
        .ctx = &e.ae_ctx, .server_id = sid, .streams = &e.streams};
    server->channels = {FakeChannel::ptr::Create(CreateWith{*e.domain}, pol)};
  }

  e.factory = std::make_unique<Factory>(e.ae_ctx, e.client, retain_connections);
  e.connections.emplace(e.ae_ctx, e.client->cloud().Load(),
                        std::move(e.factory), max_selected);

  auto& policy = *e.client->connectivity_policy().Load();
  policy.ConfigureRxTimings().ForAllPriorities(
      RxTimingConf::Every(std::chrono::seconds{30})
          .WithWindow(std::chrono::milliseconds{5}));

  e.ctx.Advance(std::chrono::milliseconds{500});
  if (start_ping_cloud) {
    e.ping_cloud =
        std::make_unique<PingCloudServers>(e.ae_ctx, *e.connections, policy);
    e.ctx.Advance(std::chrono::milliseconds{2500});
  }
  return env;
}

CloudServerConnection* FindServer(CloudServerConnections& connections,
                                  ServerId id) {
  for (auto* sc : connections.servers()) {
    if (sc->server_id() == id) {
      return sc;
    }
  }
  return nullptr;
}

void DeliverPingAck(Env& e, ServerId id) {
  auto* stream = e.streams[id];
  TEST_ASSERT_NOT_NULL(stream);
  stream->Deliver(PackEncryptedSafeApi(e.client, id, PingAckPlaintext()));
}

std::vector<DataBuffer> AuthorizedWrites(Env& e, ServerId id) {
  SyncDecryptProvider decoder{
      std::make_unique<InboundKeyProvider>(e.client, id, true)};
  std::vector<DataBuffer> result;
  for (auto const& packet : e.streams[id]->writes()) {
    PacketReader reader{packet};
    auto command = reader.TryExtract<MessageId>();
    if (!command || *command != MessageId{5}) {
      continue;
    }
    auto uid = reader.TryExtract<Uid>();
    auto ciphertext = reader.TryExtract<DataBuffer>();
    if (uid && ciphertext) {
      result.push_back(decoder.Decrypt(*ciphertext));
    }
  }
  return result;
}

int CountAuthorizedWrites(Env& e, ServerId id, MessageId command) {
  int count = 0;
  for (auto const& packet : AuthorizedWrites(e, id)) {
    count += !packet.empty() && packet.front() == command;
  }
  return count;
}

void test_LatePongReselectionCannotCancelPeriodicResume() {
  auto env = MakeEnv(false, 2, true);
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  auto& policy = *h.client->connectivity_policy().Load();
  policy.ConfigureRxTimings(RequestPolicy::All{})
      .ForAllPriorities(RxTimingConf::Every(std::chrono::milliseconds{200})
                            .WithWindow(std::chrono::milliseconds{5}));
  h.ping_cloud =
      std::make_unique<PingCloudServers>(h.ae_ctx, *h.connections, policy);
  h.ctx.Advance(std::chrono::milliseconds{300});
  auto* a = FindServer(*h.connections, kServerA);
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_TRUE(a->quarantine());
  TEST_ASSERT_TRUE(h.connections->selected_servers().empty());
  TEST_ASSERT_TRUE(PingCloudServersTestAccess::HasAwaitingLateClientPing(
      *h.ping_cloud, kServerA));

  // Both slots timed out. Recovery must fill the vacancy through Connect(),
  // whose fixture factory returns a new connection. The old Ping must retire
  // before that replacement can invalidate its completion binding.
  DeliverPingAck(h, kServerA);
  h.ctx.Advance(std::chrono::milliseconds{10});
  // The resumed Ping may itself time out under this silent mock transport;
  // the essential observation is that its request reached the new transport.
  TEST_ASSERT_GREATER_THAN(0, CountAuthorizedWrites(h, kServerA, MessageId{4}));
  TEST_ASSERT_EQUAL(0, h.streams[kServerA]->restream_count());
}

void test_NormalPongRetirementRearmsMissingPeriodicStart() {
  auto env = MakeEnv(false, 2);
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  auto& policy = *h.client->connectivity_policy().Load();
  policy.ResetRxTimings();
  policy.ConfigureRxTimings(RequestPolicy::All{})
      .ForAllPriorities(RxTimingConf::Every(std::chrono::milliseconds{10})
                            .WithWindow(std::chrono::milliseconds{5}));
  h.ctx.test_now_ = Now();
  h.ping_cloud =
      std::make_unique<PingCloudServers>(h.ae_ctx, *h.connections, policy);
  for (int tick = 0;
       tick < 40 && CountAuthorizedWrites(h, kServerA, MessageId{4}) == 0;
       ++tick) {
    h.ctx.Advance(std::chrono::milliseconds{1});
  }
  auto const before = CountAuthorizedWrites(h, kServerA, MessageId{4});
  TEST_ASSERT_GREATER_THAN(0, before);
  TEST_ASSERT_TRUE(PingCloudServersTestAccess::HasPendingClientPing(
      *h.ping_cloud, kServerA));
  // Inject the missing periodic handle seen in the live stall. A valid Pong
  // still retires its own Ping and must establish the next cadence deadline.
  PingCloudServersTestAccess::CancelPeriodicStart(*h.ping_cloud, kServerA);
  DeliverPingAck(h, kServerA);
  DeliverPingAck(h, kServerB);
  h.ctx.Advance(std::chrono::milliseconds{20});
  TEST_ASSERT_GREATER_THAN(before,
                           CountAuthorizedWrites(h, kServerA, MessageId{4}));
  TEST_ASSERT_EQUAL(0, h.streams[kServerA]->restream_count());
}

void test_NormalPongsAndDueStartsKeepCadenceForHundredCycles() {
  auto env = MakeEnv(false, 2);
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  auto& policy = *h.client->connectivity_policy().Load();
  policy.ResetRxTimings();
  policy.ConfigureRxTimings(RequestPolicy::All{})
      .ForAllPriorities(RxTimingConf::Every(std::chrono::milliseconds{1})
                            .WithWindow(std::chrono::milliseconds{1}));
  h.ping_cloud =
      std::make_unique<PingCloudServers>(h.ae_ctx, *h.connections, policy);
  std::map<ServerId, int> acknowledged;
  auto const deadline = Now() + std::chrono::seconds{5};
  while (Now() < deadline &&
         (acknowledged[kServerA] < 100 || acknowledged[kServerB] < 100)) {
    // ServerPing schedules from the real system clock, so this cadence test
    // must use the same clock rather than expiring fresh timeouts
    // synthetically.
    h.ctx.scheduler().Update(Now());
    for (auto id : {kServerA, kServerB}) {
      auto const writes = CountAuthorizedWrites(h, id, MessageId{4});
      if (writes > acknowledged[id] &&
          PingCloudServersTestAccess::HasPendingClientPing(*h.ping_cloud, id)) {
        DeliverPingAck(h, id);
        acknowledged[id] = writes;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  TEST_ASSERT_GREATER_OR_EQUAL(100, acknowledged[kServerA]);
  TEST_ASSERT_GREATER_OR_EQUAL(100, acknowledged[kServerB]);
  TEST_ASSERT_GREATER_OR_EQUAL(
      100, CountAuthorizedWrites(h, kServerA, MessageId{4}));
  TEST_ASSERT_GREATER_OR_EQUAL(
      100, CountAuthorizedWrites(h, kServerB, MessageId{4}));
  TEST_ASSERT_EQUAL(0, h.streams[kServerA]->restream_count());
  TEST_ASSERT_EQUAL(0, h.streams[kServerB]->restream_count());
}

void test_PingTimeoutSoftQuarantineKeepsInboundAndSwapsMain() {
  auto env = MakeEnv();
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  TEST_ASSERT_EQUAL(1, h.connections->selected_servers().size());
  TEST_ASSERT_NOT_NULL(FindServer(*h.connections, kServerA));
  TEST_ASSERT_NOT_NULL(FindServer(*h.connections, kServerB));
  h.ctx.Advance(std::chrono::milliseconds{500});
  ServerId ping_id = kServerA;
  if (!PingCloudServersTestAccess::HasAwaitingLateClientPing(*h.ping_cloud,
                                                             kServerA) &&
      PingCloudServersTestAccess::HasAwaitingLateClientPing(*h.ping_cloud,
                                                            kServerB)) {
    ping_id = kServerB;
  }
  TEST_ASSERT_TRUE(PingCloudServersTestAccess::HasAwaitingLateClientPing(
      *h.ping_cloud, ping_id));
  auto* timed_out = FindServer(*h.connections, ping_id);
  TEST_ASSERT_NOT_NULL(timed_out);
  TEST_ASSERT_TRUE(timed_out->quarantine());
  TEST_ASSERT_EQUAL(CloudQuarantineReason::kPingTimeoutWait,
                    timed_out->quarantine_reason());
  auto const backup_id = ping_id == kServerA ? kServerB : kServerA;
  TEST_ASSERT_EQUAL(
      backup_id,
      CloudServerConnectionsTestAccess::Main(*h.connections)->server_id());
  TEST_ASSERT_TRUE(CloudServerConnectionsTestAccess::HasErrorSubscription(
      *h.connections, *timed_out));
  TEST_ASSERT_NOT_NULL(h.streams[ping_id]);
  TEST_ASSERT_EQUAL(0, h.streams[ping_id]->restream_count());
}

void test_LatePongRecoversWithoutReconnectAndIgnoresStaleExpiry() {
  auto env = MakeEnv();
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  auto* a = FindServer(*h.connections, kServerA);
  h.ctx.Advance(std::chrono::milliseconds{500});
  TEST_ASSERT_TRUE(a->quarantine());

  DeliverPingAck(h, kServerA);
  h.ctx.Advance(std::chrono::milliseconds{50});
  TEST_ASSERT_FALSE(a->quarantine());
  TEST_ASSERT_NOT_NULL(a->client_connection());

  h.ctx.Advance(
      std::chrono::milliseconds{AE_CLOUD_SERVER_QUARANTINE_TIME_MS + 500});
  TEST_ASSERT_FALSE(a->quarantine());
  TEST_ASSERT_NOT_NULL(a->client_connection());
}

void test_LatePongStandbyDoesNotPingUntilSelectedAgain() {
  auto env = MakeEnv(false);
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  auto& policy = *h.client->connectivity_policy().Load();
  auto const main_timing = RxTimingConf::Every(std::chrono::milliseconds{300})
                               .WithWindow(std::chrono::milliseconds{5});
  auto const standby_timing =
      RxTimingConf::Every(std::chrono::milliseconds{750})
          .WithWindow(std::chrono::milliseconds{17});
  policy.ConfigureRxTimings()
      .ForAllPriorities(standby_timing)
      .ForPriority<0>(main_timing);

  auto* a = FindServer(*h.connections, kServerA);
  auto* b = FindServer(*h.connections, kServerB);
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_NOT_NULL(b);
  TEST_ASSERT_EQUAL_PTR(a,
                        CloudServerConnectionsTestAccess::Main(*h.connections));
  auto* const a_connection = a->client_connection();
  TEST_ASSERT_NOT_NULL(a_connection);

  // B must wait past A's old periodic start. Distinct service points also
  // expose a stale A overwriting the priority-zero slot after late recovery.
  Subscription quarantine_sub{
      h.connections->server_quarantined_event().Subscribe(
          [&](CloudServerConnection* server) {
            if (server != a) {
              return;
            }
            for (std::size_t priority = 0;
                 priority < policy.rx_timings().size(); ++priority) {
              policy.ReportNextServiceTime(
                  priority,
                  h.ctx.test_now_ + std::chrono::seconds{
                                        60 + 15 * static_cast<int>(priority)});
            }
          })};
  h.ping_cloud =
      std::make_unique<PingCloudServers>(h.ae_ctx, *h.connections, policy);
  h.ctx.Advance(std::chrono::milliseconds{500});
  quarantine_sub.Reset();
  TEST_ASSERT_TRUE(a->quarantine());
  TEST_ASSERT_TRUE(PingCloudServersTestAccess::HasAwaitingLateClientPing(
      *h.ping_cloud, kServerA));
  TEST_ASSERT_EQUAL_PTR(b,
                        CloudServerConnectionsTestAccess::Main(*h.connections));
  TEST_ASSERT_EQUAL(0, b->priority());
  auto const service_points = policy.rx_timings();

  DeliverPingAck(h, kServerA);
  h.ctx.Advance(std::chrono::milliseconds{50});
  TEST_ASSERT_FALSE(a->quarantine());
  TEST_ASSERT_EQUAL_PTR(a_connection, a->client_connection());
  TEST_ASSERT_EQUAL(1, a->priority());
  TEST_ASSERT_EQUAL_PTR(b,
                        CloudServerConnectionsTestAccess::Main(*h.connections));
  auto const a_write_count = h.streams[kServerA]->write_count();

  h.ctx.Advance(std::chrono::milliseconds{100});
  TEST_ASSERT_EQUAL(a_write_count, h.streams[kServerA]->write_count());
  TEST_ASSERT_NULL(
      PingCloudServersTestAccess::ClientPingPtr(*h.ping_cloud, kServerA));
  TEST_ASSERT_FALSE(a->quarantine());
  TEST_ASSERT_EQUAL_PTR(a_connection, a->client_connection());
  TEST_ASSERT_EQUAL_PTR(b,
                        CloudServerConnectionsTestAccess::Main(*h.connections));
  for (std::size_t priority = 0; priority < service_points.size(); ++priority) {
    TEST_ASSERT_TRUE(service_points[priority].next_rx_point ==
                     policy.rx_timings()[priority].next_rx_point);
  }

  // A must get a fresh ServerPing with the current priority's configuration
  // when B leaves the selected set and A fills the vacancy.
  auto const new_main_timing =
      RxTimingConf::Every(std::chrono::milliseconds{450})
          .WithWindow(std::chrono::milliseconds{9});
  policy.ConfigureRxTimings().ForPriority<0>(new_main_timing);
  TEST_ASSERT_TRUE(CloudServerConnectionsTestAccess::QuarantinePingTimeout(
      *h.connections, *b));
  CloudServerConnectionsTestAccess::ScheduleReconcile(*h.connections);
  h.ctx.Advance(std::chrono::milliseconds{50});
  TEST_ASSERT_EQUAL_PTR(a,
                        CloudServerConnectionsTestAccess::Main(*h.connections));
  TEST_ASSERT_EQUAL(0, a->priority());
  TEST_ASSERT_NOT_NULL(a->client_connection());
  TEST_ASSERT_EQUAL(LinkState::kLinked,
                    a->client_connection()->stream_info().link_state);
  // Re-selection calls Connect(), which creates a fresh transport. Compare
  // writes on that transport, while keeping synthetic time below quarantine
  // expiry so the late-response wait continues to own it.
  auto* const selected_stream = h.streams[kServerA];
  TEST_ASSERT_NOT_NULL(selected_stream);
  auto const selected_write_count = selected_stream->write_count();
  auto const earliest_service_point = Now() + new_main_timing.interval;
  PingCloudServersTestAccess::TriggerPeriodicStart(*h.ping_cloud, kServerA);
  h.ctx.Advance(std::chrono::milliseconds{20});
  TEST_ASSERT_NOT_NULL(a->client_connection());
  TEST_ASSERT_EQUAL_PTR(selected_stream, h.streams[kServerA]);
  TEST_ASSERT_GREATER_THAN(selected_write_count,
                           selected_stream->write_count());
  auto const latest_service_point = Now() + new_main_timing.interval;
  TEST_ASSERT_TRUE(earliest_service_point <=
                   policy.rx_timings()[0].next_rx_point);
  TEST_ASSERT_TRUE(policy.rx_timings()[0].next_rx_point <=
                   latest_service_point);
}

void test_ExpiryWithoutPongDisconnectsOnce() {
  auto env = MakeEnv();
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  auto* a = FindServer(*h.connections, kServerA);
  h.ctx.Advance(std::chrono::milliseconds{500});
  TEST_ASSERT_TRUE(a->quarantine());
  h.ctx.Advance(
      std::chrono::milliseconds{AE_CLOUD_SERVER_QUARANTINE_TIME_MS + 500});
  TEST_ASSERT_FALSE(a->quarantine());
  TEST_ASSERT_NULL(a->client_connection());
}

void test_PeriodicStartDoesNotReplaceAwaitingLatePing() {
  auto env = MakeEnv();
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  Ping const* before =
      PingCloudServersTestAccess::ClientPingPtr(*h.ping_cloud, kServerA);
  TEST_ASSERT_NOT_NULL(before);
  h.ctx.Advance(std::chrono::milliseconds{500});
  TEST_ASSERT_TRUE(PingCloudServersTestAccess::HasAwaitingLateClientPing(
      *h.ping_cloud, kServerA));
  PingCloudServersTestAccess::TriggerPeriodicStart(*h.ping_cloud, kServerA);
  h.ctx.Advance(std::chrono::milliseconds{50});
  TEST_ASSERT_EQUAL_PTR(before, PingCloudServersTestAccess::ClientPingPtr(
                                    *h.ping_cloud, kServerA));
}

void test_TransportErrorAfterSoftQuarantineBlocksLateRecovery() {
  auto env = MakeEnv();
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  auto* a = FindServer(*h.connections, kServerA);
  h.ctx.Advance(std::chrono::milliseconds{500});
  TEST_ASSERT_EQUAL(CloudQuarantineReason::kPingTimeoutWait,
                    a->quarantine_reason());
  auto* cc = a->client_connection();
  TEST_ASSERT_NOT_NULL(cc);
  cc->Restream();
  h.ctx.Advance(std::chrono::milliseconds{50});
  TEST_ASSERT_EQUAL(CloudQuarantineReason::kTransport, a->quarantine_reason());

  DeliverPingAck(h, kServerA);
  h.ctx.Advance(std::chrono::milliseconds{50});
  TEST_ASSERT_TRUE(a->quarantine());
}

static DataBuffer Bytes(std::initializer_list<std::uint8_t> bytes) {
  return DataBuffer{bytes};
}

void test_P2pReceivePersistsThroughSoftPingQuarantine() {
  auto env = MakeEnv();
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  h.ctx.Advance(std::chrono::milliseconds{500});

  auto& cloud = h.client->cloud_connection();
  auto& mgr = h.client->message_stream_manager();
  Uid const peer_uid{{0x11}};
  int rx_count = 0;
  auto port = mgr.CreatePort(peer_uid);
  Subscription rx_sub{
      port.out_data_event().Subscribe([&](DataBuffer const&) { ++rx_count; })};

  auto* server = CloudServerConnectionsTestAccess::Main(cloud);
  TEST_ASSERT_NOT_NULL(server);
  auto* cc = server->client_connection();
  TEST_ASSERT_NOT_NULL(cc);

  AeMessage msg1{.uid = peer_uid, .data = Bytes({1})};
  cc->client_safe_api().SendMessage(msg1);
  h.ctx.Advance(std::chrono::milliseconds{20});
  TEST_ASSERT_EQUAL(1, rx_count);

  TEST_ASSERT_FALSE(server->quarantine());

  TEST_ASSERT_TRUE(
      CloudServerConnectionsTestAccess::QuarantinePingTimeout(cloud, *server));
  TEST_ASSERT_TRUE(server->quarantine());
  TEST_ASSERT_TRUE(std::find(cloud.selected_servers().begin(),
                             cloud.selected_servers().end(),
                             server) == cloud.selected_servers().end());

  AeMessage msg2{.uid = peer_uid, .data = Bytes({2})};
  cc->client_safe_api().SendMessage(msg2);
  h.ctx.Advance(std::chrono::milliseconds{20});
  TEST_ASSERT_EQUAL_MESSAGE(
      2, rx_count,
      "inbound API must reach P2P port while server is soft-quarantined");

  TEST_ASSERT_TRUE(
      CloudServerConnectionsTestAccess::RecoverPingTimeout(cloud, *server));
  h.ctx.Advance(std::chrono::milliseconds{100});
  TEST_ASSERT_FALSE(server->quarantine());
  cc = server->client_connection();
  TEST_ASSERT_NOT_NULL(cc);

  AeMessage msg3{.uid = peer_uid, .data = Bytes({3})};
  cc->client_safe_api().SendMessage(msg3);
  h.ctx.Advance(std::chrono::milliseconds{20});
  TEST_ASSERT_EQUAL(3, rx_count);

  AeMessage msg4{.uid = peer_uid, .data = Bytes({4})};
  cc->client_safe_api().SendMessage(msg4);
  h.ctx.Advance(std::chrono::milliseconds{20});
  TEST_ASSERT_EQUAL(4, rx_count);
  (void)rx_sub;
}

void test_P2pReceiveSubscribesAfterLateLink() {
  auto env = MakeEnv();
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  auto& cloud = h.client->cloud_connection();
  auto& mgr = h.client->message_stream_manager();
  Uid const peer_uid{{0x33}};
  auto port = mgr.CreatePort(peer_uid);
  int rx_count = 0;
  Subscription rx_sub{
      port.out_data_event().Subscribe([&](DataBuffer const&) { ++rx_count; })};
  auto* server = FindServer(cloud, kServerA);
  TEST_ASSERT_NOT_NULL(server);
  server->Disconnect();
  CloudServerConnectionsTestAccess::EmitServersUpdate(cloud);
  h.ctx.Advance(std::chrono::milliseconds{20});
  TEST_ASSERT_NULL(server->client_connection());

  TEST_ASSERT_TRUE(server->Connect());
  TEST_ASSERT_TRUE(
      CloudServerConnectionsTestAccess::SubscribeServerState(cloud, *server));
  h.ctx.Advance(std::chrono::milliseconds{20});
  auto* cc = server->client_connection();
  TEST_ASSERT_NOT_NULL(cc);
  AeMessage message{.uid = peer_uid, .data = Bytes({1})};
  cc->client_safe_api().SendMessage(message);
  h.ctx.Advance(std::chrono::milliseconds{20});
  TEST_ASSERT_EQUAL_MESSAGE(1, rx_count,
                            "a late-linked server must deliver a message to "
                            "the existing P2P port once");
  (void)rx_sub;
}

void test_P2pSubscriptionClearsAfterDisconnect() {
  auto env = MakeEnv();
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  h.ctx.Advance(std::chrono::milliseconds{500});
  auto& cloud = h.client->cloud_connection();
  auto& mgr = h.client->message_stream_manager();
  Uid const peer_uid{{0x22}};
  auto port = mgr.CreatePort(peer_uid);
  int rx_count = 0;
  Subscription rx_sub{
      port.out_data_event().Subscribe([&](DataBuffer const&) { ++rx_count; })};
  auto* server = FindServer(cloud, kServerA);
  TEST_ASSERT_NOT_NULL(server);
  auto* cc = server->client_connection();
  TEST_ASSERT_NOT_NULL(cc);
  AeMessage message{.uid = peer_uid, .data = Bytes({1})};
  cc->client_safe_api().SendMessage(message);
  h.ctx.Advance(std::chrono::milliseconds{20});
  TEST_ASSERT_EQUAL(1, rx_count);
  TEST_ASSERT_TRUE(P2pMessageStreamManagerTestAccess::HasSubscriptions(mgr));

  // All receive connections must be gone before testing the real subscription
  // owner. Do not inspect or invoke the client connection after destruction.
  auto const servers = cloud.servers();
  for (auto* connected_server : servers) {
    connected_server->Disconnect();
    TEST_ASSERT_NULL(connected_server->client_connection());
  }
  CloudServerConnectionsTestAccess::EmitServersUpdate(cloud);
  TEST_ASSERT_FALSE(P2pMessageStreamManagerTestAccess::HasSubscriptions(mgr));
  h.ctx.Advance(std::chrono::milliseconds{50});
  (void)rx_sub;
  TEST_ASSERT_EQUAL(1, rx_count);
}

void test_LatePongOnOtherServerDoesNotRecoverMain() {
  auto env = MakeEnv();
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  auto* a = FindServer(*h.connections, kServerA);
  h.ctx.Advance(std::chrono::milliseconds{500});
  TEST_ASSERT_TRUE(a->quarantine());
  DeliverPingAck(h, kServerB);
  h.ctx.Advance(std::chrono::milliseconds{50});
  TEST_ASSERT_TRUE(a->quarantine());
}

}  // namespace
}  // namespace ae::test_ping_timeout_recovery

int test_ping_timeout_recovery_run() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_ping_timeout_recovery::
               test_LatePongReselectionCannotCancelPeriodicResume);
  RUN_TEST(ae::test_ping_timeout_recovery::
               test_NormalPongRetirementRearmsMissingPeriodicStart);
  RUN_TEST(ae::test_ping_timeout_recovery::
               test_NormalPongsAndDueStartsKeepCadenceForHundredCycles);
  RUN_TEST(ae::test_ping_timeout_recovery::
               test_PingTimeoutSoftQuarantineKeepsInboundAndSwapsMain);
  RUN_TEST(ae::test_ping_timeout_recovery::
               test_LatePongRecoversWithoutReconnectAndIgnoresStaleExpiry);
  RUN_TEST(ae::test_ping_timeout_recovery::
               test_LatePongStandbyDoesNotPingUntilSelectedAgain);
  RUN_TEST(
      ae::test_ping_timeout_recovery::test_ExpiryWithoutPongDisconnectsOnce);
  RUN_TEST(ae::test_ping_timeout_recovery::
               test_PeriodicStartDoesNotReplaceAwaitingLatePing);
  RUN_TEST(ae::test_ping_timeout_recovery::
               test_TransportErrorAfterSoftQuarantineBlocksLateRecovery);
  RUN_TEST(ae::test_ping_timeout_recovery::
               test_LatePongOnOtherServerDoesNotRecoverMain);
  RUN_TEST(ae::test_ping_timeout_recovery::
               test_P2pReceivePersistsThroughSoftPingQuarantine);
  RUN_TEST(
      ae::test_ping_timeout_recovery::test_P2pReceiveSubscribesAfterLateLink);
  RUN_TEST(ae::test_ping_timeout_recovery::
               test_P2pSubscriptionClearsAfterDisconnect);
  return UNITY_END();
}

#else
int test_ping_timeout_recovery_run() { return 0; }
#endif  // AE_ENABLE_PING
