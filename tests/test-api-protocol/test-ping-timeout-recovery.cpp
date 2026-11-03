/*
 * Copyright 2026 Aethernet Inc.
 *
 * Integration tests: ping timeout soft quarantine, late Pong recovery, expiry.
 */

#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <utility>

#include <unity.h>

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

namespace ae {
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

  WriteAction& Write(DataBuffer&&) override { return write_; }
  StreamUpdateEvent const& stream_update_event() override {
    return stream_update_event_;
  }
  StreamInfo stream_info() const override { return stream_info_; }
  OutDataEvent const& out_data_event() override { return out_data_event_; }
  void Restream() override { ++restream_count_; }

  void Deliver(DataBuffer data) { out_data_event_.Emit(std::move(data)); }

  ServerId server_id() const { return server_id_; }
  int restream_count() const { return restream_count_; }

 private:
  class OkWrite final : public WriteAction {
   public:
    explicit OkWrite(AeContext const& c) : WriteAction{c} {
      c.scheduler().Task([&] { SetStatus(Status::kSuccess); });
    }
  };

  AeContext context_;
  ServerId server_id_;
  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;
  OkWrite write_{context_};
  int restream_count_{0};
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

std::unique_ptr<Env> MakeEnv() {
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
    ChannelPolicy pol{.ctx = &e.ae_ctx, .server_id = sid, .streams = &e.streams};
    server->channels = {FakeChannel::ptr::Create(CreateWith{*e.domain}, pol)};
  }

  e.factory = std::make_unique<Factory>(e.ae_ctx, e.client);
  e.connections.emplace(e.ae_ctx, e.client->cloud().Load(), std::move(e.factory),
                        kMaxSelectedServers);

  auto& policy = *e.client->connectivity_policy().Load();
  policy.ConfigureRxTimings().ForAllPriorities(
      RxTimingConf::Every(std::chrono::seconds{30})
          .WithWindow(std::chrono::milliseconds{5}));

  e.ctx.Advance(std::chrono::milliseconds{500});
  e.ping_cloud = std::make_unique<PingCloudServers>(e.ae_ctx, *e.connections,
                                                    policy);
  e.ctx.Advance(std::chrono::milliseconds{2500});
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
  TEST_ASSERT_TRUE(
      PingCloudServersTestAccess::HasAwaitingLateClientPing(*h.ping_cloud,
                                                            ping_id));
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

  h.ctx.Advance(std::chrono::milliseconds{AE_CLOUD_SERVER_QUARANTINE_TIME_MS +
                                          500});
  TEST_ASSERT_FALSE(a->quarantine());
  TEST_ASSERT_NOT_NULL(a->client_connection());
}

void test_ExpiryWithoutPongDisconnectsOnce() {
  auto env = MakeEnv();
  TEST_ASSERT_NOT_NULL(env);
  auto& h = *env;
  auto* a = FindServer(*h.connections, kServerA);
  h.ctx.Advance(std::chrono::milliseconds{500});
  TEST_ASSERT_TRUE(a->quarantine());
  h.ctx.Advance(std::chrono::milliseconds{AE_CLOUD_SERVER_QUARANTINE_TIME_MS +
                                          500});
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
  TEST_ASSERT_TRUE(
      PingCloudServersTestAccess::HasAwaitingLateClientPing(*h.ping_cloud,
                                                            kServerA));
  PingCloudServersTestAccess::TriggerPeriodicStart(*h.ping_cloud, kServerA);
  h.ctx.Advance(std::chrono::milliseconds{50});
  TEST_ASSERT_EQUAL_PTR(
      before, PingCloudServersTestAccess::ClientPingPtr(*h.ping_cloud, kServerA));
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
               test_PingTimeoutSoftQuarantineKeepsInboundAndSwapsMain);
  RUN_TEST(ae::test_ping_timeout_recovery::
               test_LatePongRecoversWithoutReconnectAndIgnoresStaleExpiry);
  RUN_TEST(ae::test_ping_timeout_recovery::test_ExpiryWithoutPongDisconnectsOnce);
  RUN_TEST(ae::test_ping_timeout_recovery::
               test_PeriodicStartDoesNotReplaceAwaitingLatePing);
  RUN_TEST(ae::test_ping_timeout_recovery::
               test_TransportErrorAfterSoftQuarantineBlocksLateRecovery);
  RUN_TEST(ae::test_ping_timeout_recovery::
               test_LatePongOnOtherServerDoesNotRecoverMain);
  return UNITY_END();
}
