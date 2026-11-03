/*
 * Copyright 2026 Aethernet Inc.
 *
 * ServerPing::Start defer integration (requires AE_BENCH_PING_MIN_TIMEOUT_MS>0
 * on the test executable).
 */

#include <chrono>
#include <memory>

#include <unity.h>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "aether/adapter_registry.h"
#include "aether/aether.h"
#include "aether/channels/channel.h"
#include "aether/client.h"
#include "aether/client_connectivity_policy.h"
#include "aether/cloud.h"
#include "aether/cloud_connections/cloud_server_connection.h"
#include "aether/cloud_connections/cloud_server_connections.h"
#include "aether/cloud_connections/ping_cloud_servers.h"
#include "aether/crypto/key_gen.h"
#include "aether/env.h"
#include "aether/executors/executors.h"
#include "aether/server.h"
#include "aether/server_connections/client_server_connection.h"
#include "aether/server_connections/iserver_connection_factory.h"
#include "aether/socket_initializer.h"
#include "aether/stream_api/istream.h"
#include "aether/tasks/manual_task_scheduler.h"
#include "aether/types/address.h"
#include "aether/write_action/write_action.h"

namespace ae::test_ping_server_defer {
namespace {

SocketInitializer g_socket_initializer;

constexpr ServerId kServerId{42};
constexpr ServerId kServerIdB{43};

using Scheduler = ManualTaskScheduler<TaskManagerConf<128>>;

struct TestContext : public AeEnv {
  template <typename... A>
  decltype(auto) Update(A&&... a) {
    return sched.Update(std::forward<A>(a)...);
  }
  void* find_component(EnvId id) noexcept override {
    if (id == EnvTypeId<TaskScheduler>::value) {
      return &sched;
    }
    if (id == EnvTypeId<EventSystem>::value) {
      return &event_system;
    }
    return nullptr;
  }
  void Advance(Duration delta) {
    auto const end = test_now_ + delta;
    while (test_now_ < end) {
      test_now_ += std::chrono::milliseconds{1};
      sched.Update(test_now_);
    }
  }
  TimePoint test_now_{std::chrono::system_clock::now()};
  Scheduler sched;
  EventSystem event_system;
};

class LinkedMockStream final : public ByteIStream {
 public:
  explicit LinkedMockStream(AeContext const& context)
      : context_{context},
        stream_info_{512, 1024, true, LinkState::kLinked, true},
        out_data_event_{context_},
        stream_update_event_{context_} {}
  WriteAction& Write(DataBuffer&&) override { return write_; }
  StreamUpdateEvent const& stream_update_event() override {
    return stream_update_event_;
  }
  StreamInfo stream_info() const override { return stream_info_; }
  OutDataEvent const& out_data_event() override { return out_data_event_; }
  void Restream() override {}

 private:
  class OkWrite final : public WriteAction {
   public:
    explicit OkWrite(AeContext const& c) : WriteAction{c} {
      c.scheduler().Task([&] { SetStatus(Status::kSuccess); });
    }
  };
  AeContext context_;
  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;
  OkWrite write_{context_};
};

struct ChannelPolicy {
  AeContext const* ctx{nullptr};
};

class FakeChannel final : public Channel {
  AE_OBJECT(FakeChannel, Channel, 3)

 protected:
  FakeChannel() = default;

 public:
  FakeChannel(ObjProp prop, ChannelPolicy policy) : Channel{prop}, policy_{policy} {
    transport_properties_.max_packet_size = 1024;
    transport_properties_.rec_packet_size = 512;
    transport_properties_.connection_type = ConnectionType::kConnectionFull;
    transport_properties_.reliability = Reliability::kReliable;
  }
  AE_OBJECT_REFLECT()
  TransportBuildSender TransportBuilder() override {
    return ex::just(std::unique_ptr<ByteIStream>{
        std::make_unique<LinkedMockStream>(*policy_.ctx)});
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
  Factory(AeContext ctx, ObjPtr<Client> client, ChannelPolicy policy)
      : ctx_{ctx}, client_{client}, policy_{policy} {}
  std::shared_ptr<ClientServerConnection> CreateConnection(
      Ptr<Server> const& server) override {
    return std::make_shared<ClientServerConnection>(ctx_, client_.Load(),
                                                    server);
  }

 private:
  AeContext ctx_;
  ObjPtr<Client> client_;
  ChannelPolicy policy_;
};

}  // namespace

void test_ServerPingPeriodicStartDefersWhileClientPingPending() {
#if AE_BENCH_PING_MIN_TIMEOUT_MS > 0
  TestContext ctx;
  RamDomainStorage storage;
  Domain domain{storage, &ctx};
  AeContext ae_ctx{ctx};
  auto aether = Aether::ptr::Create(CreateWith{domain});
  auto registry = AdapterRegistry::ptr::Create(CreateWith{domain});
  aether->adapter_registry = registry;
  ctx.SetAether(aether);
  aether->client_prefab = Client::ptr::Create(CreateWith{domain}, aether);
  aether->client_prefab.Save();

  Key master_key{};
  TEST_ASSERT_TRUE(CryptoSyncKeygen(master_key));
  Endpoint ep{{IpV4Addr{{192, 0, 2, 1}}, 9021}, Protocol::kUdp};
  ClientConfig config{.parent_uid = Uid{{1}},
                      .uid = Uid{{2}},
                      .ephemeral_uid = Uid{{3}},
                      .master_key = std::move(master_key),
                      .cloud = {{kServerId, {ep}}, {kServerIdB, {ep}}}};
  auto client = aether->CreateClient(config, "ping-server-defer");

  ChannelPolicy pol{.ctx = &ae_ctx};
  for (auto sid : {kServerId, kServerIdB}) {
    auto server = aether->GetServer(sid).Load();
    server->channels = {FakeChannel::ptr::Create(CreateWith{domain}, pol)};
  }

  auto factory =
      std::make_unique<Factory>(ae_ctx, client, pol);
  CloudServerConnections cloud_connections(
      ae_ctx, client->cloud().Load(), std::move(factory),
      AE_CLOUD_MAX_SERVER_CONNECTIONS);
  ClientConnectivityPolicy::ptr const& policy_ref = client->connectivity_policy();
  TEST_ASSERT_TRUE(policy_ref.is_valid());
  auto& policy_obj = *policy_ref.Load();
  policy_obj
      .ConfigureRxTimings()
      .ForAllPriorities(RxTimingConf::Every(std::chrono::seconds{30})
                            .WithWindow(std::chrono::milliseconds{5}));

  PingCloudServers ping_cloud(ae_ctx, cloud_connections, policy_obj);
  ctx.Advance(std::chrono::milliseconds{2000});

  TEST_ASSERT_TRUE(
      PingCloudServersTestAccess::HasPendingClientPing(ping_cloud, kServerId));
  Ping const* const before =
      PingCloudServersTestAccess::ClientPingPtr(ping_cloud, kServerId);
  TEST_ASSERT_NOT_NULL(before);

  PingCloudServersTestAccess::TriggerPeriodicStart(ping_cloud, kServerId);
  ctx.Advance(std::chrono::milliseconds{50});

  TEST_ASSERT_TRUE(
      PingCloudServersTestAccess::HasPendingClientPing(ping_cloud, kServerId));
  TEST_ASSERT_EQUAL_PTR(
      before, PingCloudServersTestAccess::ClientPingPtr(ping_cloud, kServerId));
#else
  TEST_MESSAGE(
      "AE_BENCH_PING_MIN_TIMEOUT_MS=0; build test-api-protocol-ping-push-ack "
      "for ServerPing defer coverage");
#endif
}

}  // namespace ae::test_ping_server_defer

int test_ping_server_defer_run() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_ping_server_defer::
               test_ServerPingPeriodicStartDefersWhileClientPingPending);
  return UNITY_END();
}
