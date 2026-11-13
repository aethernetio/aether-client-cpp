/*
 * Copyright 2026 Aethernet Inc.
 *
 * ServerPing / PingCloudServers lifetime and deferred-task correlation tests.
 */

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <utility>

#include <unity.h>

#include "aether/tasks/details/task_subsctiption.h"

#include "ping_cloud_servers_test_access.h"

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "aether/adapter_registry.h"
#include "aether/aether.h"
#include "aether/api_protocol/api_protocol.h"
#include "aether/channels/channel.h"
#include "aether/client.h"
#include "aether/client_connectivity_policy.h"
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
#include "aether/write_action/write_action.h"

#if AE_ENABLE_PING
namespace ae::test_ping_serverping_lifecycle {
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

  WriteAction& Write(DataBuffer&&) override {
    if (on_write_hook_) {
      on_write_hook_();
    }
    return write_;
  }
  StreamUpdateEvent const& stream_update_event() override {
    return stream_update_event_;
  }
  StreamInfo stream_info() const override { return stream_info_; }
  OutDataEvent const& out_data_event() override { return out_data_event_; }
  void Restream() override { ++restream_count_; }

  void Deliver(DataBuffer data) { out_data_event_.Emit(std::move(data)); }

  void SetWriteHook(std::function<void()> hook) {
    on_write_hook_ = std::move(hook);
  }

  ServerId server_id() const { return server_id_; }
  int restream_count() const { return restream_count_; }

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
  int restream_count_{0};
  std::function<void()> on_write_hook_;
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
  Factory(AeContext ctx, ObjPtr<Client> client) : ctx_{ctx}, client_{client} {}
  std::shared_ptr<ClientServerConnection> CreateConnection(
      Ptr<Server> const& server) override {
    return std::make_shared<ClientServerConnection>(ctx_, client_.Load(), server);
  }

 private:
  AeContext ctx_;
  ObjPtr<Client> client_;
};

class InboundKeyProvider final : public ISyncKeyProvider {
 public:
  InboundKeyProvider(ObjPtr<Client> client, ServerId server_id)
      : client_{client}, server_id_{server_id} {}
  Key GetKey() const override {
    return client_.Load()->server_state(server_id_)->server_to_client();
  }
  CryptoNonce const& Nonce() const override {
    auto* st = client_.Load()->server_state(server_id_);
    st->Next();
    return st->nonce();
  }

 private:
  ObjPtr<Client> client_;
  ServerId server_id_;
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

std::unique_ptr<Env> MakeEnv(
    std::chrono::milliseconds ping_interval = std::chrono::seconds{30},
    bool start_ping_cloud = true) {
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
  e.client = aether->CreateClient(config, "ping-serverping-lifecycle");

  for (auto sid : {kServerA, kServerB}) {
    auto server = aether->GetServer(sid).Load();
    ChannelPolicy pol{.ctx = &e.ae_ctx, .server_id = sid, .streams = &e.streams};
    server->channels = {FakeChannel::ptr::Create(CreateWith{*e.domain}, pol)};
  }

  e.factory = std::make_unique<Factory>(e.ae_ctx, e.client);
  e.connections.emplace(e.ae_ctx, e.client->cloud().Load(), std::move(e.factory),
                        kMaxSelectedServers);

  auto& policy = *e.client->connectivity_policy().Load();
  policy.ConfigureRxTimings().ForAllPriorities(
      RxTimingConf::Every(ping_interval)
          .WithWindow(std::chrono::milliseconds{5}));

  e.ctx.Advance(std::chrono::milliseconds{500});
  if (start_ping_cloud) {
    e.ping_cloud = std::make_unique<PingCloudServers>(e.ae_ctx, *e.connections,
                                                      policy);
    e.ctx.Advance(std::chrono::milliseconds{2500});
  }
  return env;
}

void StartPingCloud(Env& e) {
  auto& policy = *e.client->connectivity_policy().Load();
  e.ping_cloud = std::make_unique<PingCloudServers>(e.ae_ctx, *e.connections,
                                                    policy);
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

ServerId TimedOutServer(Env& h) {
  if (PingCloudServersTestAccess::HasAwaitingLateClientPing(*h.ping_cloud,
                                                            kServerA)) {
    return kServerA;
  }
  if (PingCloudServersTestAccess::HasAwaitingLateClientPing(*h.ping_cloud,
                                                            kServerB)) {
    return kServerB;
  }
  return kServerA;
}

void test_SyncAckDuringWriteViaServerPingCompletes() {
  auto env = MakeEnv(std::chrono::seconds{30}, false);
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  auto* stream = h.streams[kServerA];
  TEST_ASSERT_NOT_NULL(stream);
  stream->SetWriteHook([&]() { DeliverPingAck(h, kServerA); });
  StartPingCloud(h);
  h.ctx.Advance(std::chrono::milliseconds{150});
  TEST_ASSERT_FALSE(
      PingCloudServersTestAccess::HasPendingClientPing(*h.ping_cloud, kServerA));
  TEST_ASSERT_FALSE(PingCloudServersTestAccess::HasAwaitingLateClientPing(
      *h.ping_cloud, kServerA));
}

void test_DestroyPingCloudAfterTimeoutBeforeDeferredQuarantine() {
  auto env = MakeEnv();
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  h.ctx.Advance(std::chrono::milliseconds{500});
  ServerId const sid = TimedOutServer(h);
  TEST_ASSERT_TRUE(
      PingCloudServersTestAccess::HasAwaitingLateClientPing(*h.ping_cloud,
                                                            sid));
  auto* sc = FindServer(*h.connections, sid);
  TEST_ASSERT_NOT_NULL(sc);
  h.ping_cloud.reset();
  h.ctx.Advance(std::chrono::milliseconds{200});
}

void test_StopServerPingBeforeLateRecoveryDoesNotRecover() {
  auto env = MakeEnv();
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  h.ctx.Advance(std::chrono::milliseconds{500});
  ServerId const sid = TimedOutServer(h);
  auto* sc = FindServer(*h.connections, sid);
  TEST_ASSERT_TRUE(sc->quarantine());
  DeliverPingAck(h, sid);
  PingCloudServersTestAccess::StopServerPing(*h.ping_cloud, sid);
  h.ctx.Advance(std::chrono::milliseconds{50});
  TEST_ASSERT_TRUE(sc->quarantine());
}

void test_TransportRestreamBeforeLateRecoveryDoesNotClearQuarantine() {
  auto env = MakeEnv();
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  h.ctx.Advance(std::chrono::milliseconds{500});
  ServerId const sid = TimedOutServer(h);
  auto* sc = FindServer(*h.connections, sid);
  TEST_ASSERT_TRUE(sc->quarantine());
  auto* cc = sc->client_connection();
  TEST_ASSERT_NOT_NULL(cc);
  cc->Restream();
  h.ctx.Advance(std::chrono::milliseconds{20});
  DeliverPingAck(h, sid);
  h.ctx.Advance(std::chrono::milliseconds{50});
  TEST_ASSERT_TRUE(sc->quarantine());
}

void test_TimeoutDeferredThenLatePongOnAdjacentSchedulerSteps() {
  auto env = MakeEnv();
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  h.ctx.Advance(std::chrono::milliseconds{500});
  ServerId const sid = TimedOutServer(h);
  auto* sc = FindServer(*h.connections, sid);
  TEST_ASSERT_TRUE(sc->quarantine());
  DeliverPingAck(h, sid);
  for (int step = 0; step < 8 && sc->quarantine(); ++step) {
    h.ctx.Advance(std::chrono::milliseconds{1});
  }
  TEST_ASSERT_FALSE(sc->quarantine());
  TEST_ASSERT_NOT_NULL(sc->client_connection());
}

void test_LatePongJustBeforeExpirySingleOutcome() {
  auto env = MakeEnv();
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  h.ctx.Advance(std::chrono::milliseconds{500});
  ServerId const sid = TimedOutServer(h);
  auto* sc = FindServer(*h.connections, sid);
  TEST_ASSERT_TRUE(sc->quarantine());
  auto* cc_before = sc->client_connection();
  DeliverPingAck(h, sid);
  h.ctx.Advance(std::chrono::milliseconds{5});
  TEST_ASSERT_FALSE(sc->quarantine());
  TEST_ASSERT_EQUAL_PTR(cc_before, sc->client_connection());
  h.ctx.Advance(std::chrono::milliseconds{AE_CLOUD_SERVER_QUARANTINE_TIME_MS});
  TEST_ASSERT_FALSE(sc->quarantine());
  TEST_ASSERT_NOT_NULL(sc->client_connection());
}

void test_RecoveryThenNextPeriodicPingRuns() {
  auto env = MakeEnv();
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  h.ctx.Advance(std::chrono::milliseconds{500});
  ServerId const sid = TimedOutServer(h);
  auto* sc = FindServer(*h.connections, sid);
  TEST_ASSERT_TRUE(sc->quarantine());
  DeliverPingAck(h, sid);
  h.ctx.Advance(std::chrono::milliseconds{50});
  TEST_ASSERT_FALSE(sc->quarantine());
  TEST_ASSERT_NOT_NULL(sc->client_connection());
  TEST_ASSERT_EQUAL(1, h.connections->selected_servers().size());
  TEST_ASSERT_NOT_EQUAL(sid,
                        h.connections->selected_servers().front()->server_id());
  PingCloudServersTestAccess::TriggerPeriodicStart(*h.ping_cloud, sid);
  h.ctx.Advance(std::chrono::milliseconds{100});
  TEST_ASSERT_NULL(
      PingCloudServersTestAccess::ClientPingPtr(*h.ping_cloud, sid));
}

void test_NewClientConnectionBeforeLateRecoveryIgnored() {
  auto env = MakeEnv();
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  h.ctx.Advance(std::chrono::milliseconds{500});
  ServerId const sid = TimedOutServer(h);
  auto* sc = FindServer(*h.connections, sid);
  TEST_ASSERT_TRUE(sc->quarantine());
  auto* old_cc = sc->client_connection();
  TEST_ASSERT_NOT_NULL(old_cc);
  sc->Disconnect();
  TEST_ASSERT_TRUE(sc->Connect());
  h.ctx.Advance(std::chrono::milliseconds{200});
  TEST_ASSERT_NOT_NULL(sc->client_connection());
  if (old_cc != sc->client_connection()) {
    DeliverPingAck(h, sid);
    h.ctx.Advance(std::chrono::milliseconds{50});
    TEST_ASSERT_TRUE(sc->quarantine());
  }
}

void test_StaleAckAfterSecondPingStartedViaWireDecode() {
  auto env = MakeEnv(std::chrono::milliseconds{200});
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  h.ctx.Advance(std::chrono::milliseconds{500});
  ServerId const sid = TimedOutServer(h);
  DeliverPingAck(h, sid);
  h.ctx.Advance(std::chrono::milliseconds{50});
  PingCloudServersTestAccess::TriggerPeriodicStart(*h.ping_cloud, sid);
  h.ctx.Advance(std::chrono::milliseconds{80});
  Ping const* second_ping =
      PingCloudServersTestAccess::ClientPingPtr(*h.ping_cloud, sid);
  TEST_ASSERT_NOT_NULL(second_ping);
  bool const second_pending = second_ping->IsPending();
  DeliverPingAck(h, sid);
  h.ctx.Advance(std::chrono::milliseconds{50});
  if (second_pending) {
    TEST_ASSERT_FALSE(PingCloudServersTestAccess::HasPendingClientPing(
        *h.ping_cloud, sid));
  }
}

}  // namespace
}  // namespace ae::test_ping_serverping_lifecycle

int test_ping_serverping_lifecycle_run() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_ping_serverping_lifecycle::
               test_SyncAckDuringWriteViaServerPingCompletes);
  RUN_TEST(ae::test_ping_serverping_lifecycle::
               test_DestroyPingCloudAfterTimeoutBeforeDeferredQuarantine);
  RUN_TEST(ae::test_ping_serverping_lifecycle::
               test_StopServerPingBeforeLateRecoveryDoesNotRecover);
  RUN_TEST(ae::test_ping_serverping_lifecycle::
               test_TransportRestreamBeforeLateRecoveryDoesNotClearQuarantine);
  RUN_TEST(ae::test_ping_serverping_lifecycle::
               test_TimeoutDeferredThenLatePongOnAdjacentSchedulerSteps);
  RUN_TEST(ae::test_ping_serverping_lifecycle::
               test_LatePongJustBeforeExpirySingleOutcome);
  RUN_TEST(ae::test_ping_serverping_lifecycle::
               test_RecoveryThenNextPeriodicPingRuns);
  RUN_TEST(ae::test_ping_serverping_lifecycle::
               test_NewClientConnectionBeforeLateRecoveryIgnored);
  RUN_TEST(ae::test_ping_serverping_lifecycle::
               test_StaleAckAfterSecondPingStartedViaWireDecode);
  return UNITY_END();
}

#else
int test_ping_serverping_lifecycle_run() { return 0; }
#endif  // AE_ENABLE_PING
