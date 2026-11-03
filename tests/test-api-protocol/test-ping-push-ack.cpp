
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
 * See the License for the License.
 */

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <unity.h>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "aether/adapter_registry.h"
#include "aether/ae_actions/ping.h"
#include "aether/aether.h"
#include "aether/api_protocol/api_protocol.h"
#include "aether/channels/channel.h"
#include "aether/client.h"
#include "aether/cloud.h"
#include "aether/cloud_connections/cloud_server_connection.h"
#include "aether/crypto/key_gen.h"
#include "aether/diagnostics/bench_ping_server_start_defer.h"
#include "aether/env.h"
#include "aether/executors/executors.h"
#include "aether/server.h"
#include "aether/server_connections/client_server_connection.h"
#include "aether/socket_initializer.h"
#include "aether/server_connections/iserver_connection_factory.h"
#include "aether/stream_api/istream.h"
#include "aether/tasks/details/task_subsctiption.h"
#include "aether/tasks/manual_task_scheduler.h"
#include "aether/types/address.h"
#include "aether/types/server_id.h"
#include "aether/write_action/write_action.h"

namespace ae::test_ping_push_ack {
namespace {

SocketInitializer g_socket_initializer;

constexpr auto kServerId = ServerId{42};
constexpr auto kServerIdB = ServerId{43};
constexpr auto kPingAckMessageId = MessageId{24};
constexpr auto kEndpointPort = std::uint16_t{9021};

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

  void PumpFor(Duration delta) {
    auto t = std::chrono::system_clock::now();
    auto const end = t + delta;
    while (t < end) {
      t += std::chrono::milliseconds{1};
      sched.Update(t);
    }
  }

  void PumpFromUntil(TimePoint start, TimePoint end) {
    auto t = start;
    while (t < end) {
      t += std::chrono::milliseconds{1};
      sched.Update(t);
    }
  }

  Scheduler sched;
  EventSystem event_system;
};

Endpoint UdpEndpoint(std::uint16_t port) {
  return Endpoint{{IpV4Addr{{192, 0, 2, 1}}, port}, Protocol::kUdp};
}

class ImmediateWriteAction : public WriteAction {
 public:
  explicit ImmediateWriteAction(AeContext const& context)
      : WriteAction{context} {
    context.scheduler().Task(
        [&]() { WriteAction::SetStatus(Status::kSuccess); });
  }
};

class FailWriteAction : public WriteAction {
 public:
  explicit FailWriteAction(AeContext const& context) : WriteAction{context} {
    context.scheduler().Task(
        [&]() { WriteAction::SetStatus(Status::kFail); });
  }
};

inline WriteAction& SelectWriteAction(
    AeContext const& context, bool fail_write,
    std::optional<ImmediateWriteAction>& ok,
    std::optional<FailWriteAction>& fail) {
  if (fail_write) {
    return fail.emplace(context);
  }
  return ok.emplace(context);
}

class LinkedMockStream final : public ByteIStream {
 public:
  explicit LinkedMockStream(AeContext const& context, bool fail_write)
      : context_{context},
        fail_write_{fail_write},
        stream_info_{512, 1024, true, LinkState::kLinked, true},
        out_data_event_{context_},
        stream_update_event_{context_} {}

  WriteAction& Write(DataBuffer&& /*data*/) override {
    return SelectWriteAction(context_, fail_write_, ok_action_, fail_action_);
  }
  StreamUpdateEvent const& stream_update_event() override {
    return stream_update_event_;
  }
  StreamInfo stream_info() const override { return stream_info_; }
  OutDataEvent const& out_data_event() override { return out_data_event_; }
  void Restream() override {}

 private:
  AeContext context_;
  bool fail_write_;
  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;
  std::optional<ImmediateWriteAction> ok_action_;
  std::optional<FailWriteAction> fail_action_;
};

struct FakeBuildPolicy {
  AeContext const* context{nullptr};
  bool fail_write{false};
};

class PingPushAckFakeChannel final : public Channel {
  AE_OBJECT(PingPushAckFakeChannel, Channel, 1)

 protected:
  PingPushAckFakeChannel() = default;

 public:
  PingPushAckFakeChannel(ObjProp prop, FakeBuildPolicy policy)
      : Channel{prop}, policy_{policy} {
    transport_properties_.max_packet_size = 1024;
    transport_properties_.rec_packet_size = 512;
    transport_properties_.connection_type = ConnectionType::kConnectionFull;
    transport_properties_.reliability = Reliability::kReliable;
  }

  AE_OBJECT_REFLECT()

  TransportBuildSender TransportBuilder() override {
    assert(policy_.context != nullptr);
    return ex::just(std::unique_ptr<ByteIStream>{std::make_unique<LinkedMockStream>(
        *policy_.context, policy_.fail_write)});
  }

  Duration TransportBuildTimeout() const override {
    return std::chrono::milliseconds{50};
  }
  Duration ResponseTimeout() const override {
    return std::chrono::milliseconds{50};
  }
  std::optional<Endpoint> endpoint() const override { return std::nullopt; }

 private:
  FakeBuildPolicy policy_;
};

class LinkedClientFactory final : public IServerConnectionFactory {
 public:
  LinkedClientFactory(AeContext ae_context, ObjPtr<Client> client,
                      FakeBuildPolicy policy)
      : ae_context_{ae_context},
        client_{std::move(client)},
        policy_{policy} {}

  std::shared_ptr<ClientServerConnection> CreateConnection(
      Ptr<Server> const& server) override {
    auto client = client_.Load();
    return std::make_shared<ClientServerConnection>(ae_context_, client,
                                                    server);
  }

 private:
  AeContext ae_context_;
  ObjPtr<Client> client_;
  FakeBuildPolicy policy_;
};

struct PingHarness {
  static constexpr Duration kPingTimeout{std::chrono::milliseconds{100}};
  static constexpr Duration kRxWindow{std::chrono::milliseconds{5}};
  static constexpr Duration kHint{std::chrono::seconds{1}};

  PingHarness(bool fail_write = false)
      : ae_ctx{ctx}, domain{storage, &ctx} {
    aether = Aether::ptr::Create(CreateWith{domain});
    registry = AdapterRegistry::ptr::Create(CreateWith{domain});
    aether->adapter_registry = registry;
    ctx.SetAether(aether);
    aether->client_prefab = Client::ptr::Create(CreateWith{domain}, aether);
    aether->client_prefab.Save();

    Key master_key{};
    if (!CryptoSyncKeygen(master_key)) {
      return;
    }
    ClientConfig config{
        .parent_uid = Uid{{1}},
        .uid = Uid{{2}},
        .ephemeral_uid = Uid{{3}},
        .master_key = std::move(master_key),
        .cloud = {{kServerId, {UdpEndpoint(kEndpointPort)}},
                  {kServerIdB, {UdpEndpoint(kEndpointPort + 1)}}},
    };
    client = aether->CreateClient(config, "ping-push-ack-client");

    policy.context = &ae_ctx;
    policy.fail_write = fail_write;
    ready = AttachLinkedServer(kServerId) && AttachLinkedServer(kServerIdB) &&
            ConnectCloud(kServerId);
  }

  bool AttachLinkedServer(ServerId id) {
    auto server = aether->GetServer(id).Load();
    if (server == nullptr) {
      return false;
    }
    server->channels = {
        PingPushAckFakeChannel::ptr::Create(CreateWith{domain}, policy)};
    return true;
  }

  bool ConnectCloud(ServerId id) {
    factory = std::make_unique<LinkedClientFactory>(ae_ctx, client, policy);
    if (!client->cloud()) {
      return false;
    }
    auto cloud = client->cloud().Load();
    cloud_conn = std::make_unique<CloudServerConnection>(cloud, id, *factory);
    if (!cloud_conn->Connect()) {
      return false;
    }
    ctx.PumpFor(std::chrono::milliseconds{200});
    auto* cc = cloud_conn->client_connection();
    if (cc == nullptr) {
      return false;
    }
    return cc->stream_info().link_state == LinkState::kLinked;
  }

  bool ready{false};

  ClientServerConnection* Connection() {
    return cloud_conn->client_connection();
  }

  CloudServerConnection& CloudConn() { return *cloud_conn; }

  TestContext ctx;
  RamDomainStorage storage;
  Domain domain;
  AeContext ae_ctx;
  Aether::ptr aether;
  AdapterRegistry::ptr registry;
  ObjPtr<Client> client;
  FakeBuildPolicy policy{};
  std::unique_ptr<LinkedClientFactory> factory;
  std::unique_ptr<CloudServerConnection> cloud_conn;
};

struct ResultCapture {
  int count{0};
  std::optional<Ping::PingResult> last;
  Subscription sub;

  void Attach(Ping const& ping, EventSystem& es) {
    sub = ping.result_event().Subscribe([this](Ping::PingResult const& res) {
      ++count;
      last = res;
    });
    (void)es;
  }

  bool HasOk() const {
    return last.has_value() && std::holds_alternative<Ok<Duration>>(*last);
  }
  bool HasTimeout() const {
    return last.has_value() && std::holds_alternative<Error<int>>(*last) &&
           std::get<Error<int>>(*last).error == 2;
  }
  bool HasLate() const {
    return last.has_value() &&
           std::holds_alternative<Ping::LateDuration>(*last);
  }
  bool HasWriteFail() const {
    return last.has_value() && std::holds_alternative<Error<int>>(*last) &&
           std::get<Error<int>>(*last).error == 1;
  }
};

std::unique_ptr<Ping> MakePing(PingHarness& h) {
  return std::unique_ptr<Ping>(new Ping(
      h.ae_ctx, h.CloudConn(), PingHarness::kHint, PingHarness::kRxWindow,
      PingHarness::kPingTimeout, PingHarness::kPingTimeout));
}

void StartPing(PingHarness& h, Ping& ping, ResultCapture& cap) {
  cap.Attach(ping, h.ctx.event_system);
  ping.Start(std::chrono::system_clock::now());
  h.ctx.PumpFor(std::chrono::milliseconds{20});
}

DataBuffer MakeSerializedPingAckPacket() {
  DataBuffer packet;
  ApiPacker packer{packet};
  packer.Pack(kPingAckMessageId, GenericMessage<>{});
  return packet;
}

void DeliverSerializedPingAck(ClientServerConnection& connection) {
  ClientServerConnectionTestAccess::DeliverInboundSafeApiPlaintext(
      connection, MakeSerializedPingAckPacket());
}

}  // namespace

void test_PushPingAckCompletesPingBeforeTimeout() {
  PingHarness h;
  TEST_ASSERT_TRUE(h.ready);
  auto ping = MakePing(h);
  ResultCapture cap;
  StartPing(h, *ping, cap);

  DeliverSerializedPingAck(*h.Connection());
  h.ctx.PumpFor(std::chrono::milliseconds{30});

  TEST_ASSERT_EQUAL(1, cap.count);
  TEST_ASSERT_TRUE(cap.HasOk());
}

void test_PingPushAckRegressionSuite() {
  {
    PingHarness h;
    TEST_ASSERT_TRUE(h.ready);
    auto ping = MakePing(h);
    ResultCapture cap;
    cap.Attach(*ping, h.ctx.event_system);
    ping->Start(std::chrono::system_clock::now());
    DeliverSerializedPingAck(*h.Connection());
    h.ctx.PumpFor(std::chrono::milliseconds{150});
    TEST_ASSERT_EQUAL(1, cap.count);
    TEST_ASSERT_TRUE(cap.HasOk());
  }

  {
    PingHarness h;
    TEST_ASSERT_TRUE(h.ready);
    auto ping = MakePing(h);
    ResultCapture cap;
    StartPing(h, *ping, cap);
    DeliverSerializedPingAck(*h.Connection());
    DeliverSerializedPingAck(*h.Connection());
    h.ctx.PumpFor(std::chrono::milliseconds{150});
    TEST_ASSERT_EQUAL(1, cap.count);
    TEST_ASSERT_TRUE(cap.HasOk());
  }

  {
    PingHarness h;
    TEST_ASSERT_TRUE(h.ready);
    auto ping = MakePing(h);
    ResultCapture cap;
    cap.Attach(*ping, h.ctx.event_system);
    ping->Start(std::chrono::system_clock::now());
    h.ctx.PumpFor(std::chrono::milliseconds{10});
    ping.reset();
    h.ctx.PumpFor(std::chrono::milliseconds{150});
  }

  {
    PingHarness h{true};
    TEST_ASSERT_TRUE(h.ready);
    auto ping = MakePing(h);
    ResultCapture cap;
    StartPing(h, *ping, cap);
    h.ctx.PumpFor(std::chrono::milliseconds{150});
    TEST_ASSERT_EQUAL(1, cap.count);
    TEST_ASSERT_TRUE(cap.HasWriteFail());
    DeliverSerializedPingAck(*h.Connection());
    h.ctx.PumpFor(std::chrono::milliseconds{150});
    TEST_ASSERT_EQUAL(1, cap.count);
  }
}

void test_DeferPeriodicStartWhilePingPending() {
#if AE_BENCH_PING_MIN_TIMEOUT_MS > 0
  bench_ping_server_start::DeferState state{};
  TEST_ASSERT_TRUE(
      bench_ping_server_start::TryDeferPeriodicStart(state, true, true));
  TEST_ASSERT_TRUE(state.deferred_start_after_ping);
  TEST_ASSERT_FALSE(
      bench_ping_server_start::TryDeferPeriodicStart(state, true, false));
#else
  TEST_MESSAGE(
      "AE_BENCH_PING_MIN_TIMEOUT_MS=0 in this build; defer guard not compiled");
#endif
}

}  // namespace ae::test_ping_push_ack

int test_api_protocol_ping_push_ack() {
  UNITY_BEGIN();
  using namespace ae::test_ping_push_ack;  // NOLINT

  RUN_TEST(test_DeferPeriodicStartWhilePingPending);
  RUN_TEST(test_PushPingAckCompletesPingBeforeTimeout);

  return UNITY_END();
}
