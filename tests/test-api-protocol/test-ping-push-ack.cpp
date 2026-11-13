
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

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
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
#include "aether/crypto/ikey_provider.h"
#include "aether/crypto/key_gen.h"
#include "aether/crypto/sync_crypto_provider.h"
#include "aether/env.h"
#include "aether/executors/executors.h"
#include "aether/server.h"
#include "aether/server_connections/client_server_connection.h"
#include "aether/server_connections/iserver_connection_factory.h"
#include "aether/socket_initializer.h"
#include "aether/stream_api/istream.h"
#include "aether/tasks/details/task_subsctiption.h"
#include "aether/tasks/manual_task_scheduler.h"
#include "aether/types/address.h"
#include "aether/types/server_id.h"
#include "aether/write_action/write_action.h"

#if AE_ENABLE_PING
namespace ae::test_ping_push_ack {
namespace {

SocketInitializer g_socket_initializer;

constexpr auto kServerId = ServerId{42};
constexpr auto kServerIdB = ServerId{43};
constexpr auto kPingAckMessageId = MessageId{24};
constexpr auto kSendSafeApiMessageId = MessageId{4};
constexpr auto kEndpointPort = std::uint16_t{9021};

struct TestContext : public AeEnv {
  TimePoint Now() const { return test_now_; }

  void Advance(Duration delta) {
    auto const end = test_now_ + delta;
    while (test_now_ < end) {
      test_now_ += std::chrono::milliseconds{1};
      scheduler().Update(test_now_);
    }
  }

  void AdvanceUntil(TimePoint end) {
    while (test_now_ < end) {
      test_now_ += std::chrono::milliseconds{1};
      scheduler().Update(test_now_);
    }
  }

  TimePoint test_now_{std::chrono::system_clock::now()};
};

Endpoint UdpEndpoint(std::uint16_t port) {
  return Endpoint{{IpV4Addr{{192, 0, 2, 1}}, port}, Protocol::kUdp};
}

class ImmediateWriteAction : public WriteAction {
 public:
  explicit ImmediateWriteAction(AeContext const& context)
      : WriteAction{context} {
    completion_sub_ = context.scheduler().Task(
        [this]() { WriteAction::SetStatus(Status::kSuccess); });
  }

 private:
  TaskSubscription completion_sub_;
};

class FailWriteAction : public WriteAction {
 public:
  explicit FailWriteAction(AeContext const& context) : WriteAction{context} {
    completion_sub_ = context.scheduler().Task(
        [this]() { WriteAction::SetStatus(Status::kFail); });
  }

 private:
  TaskSubscription completion_sub_;
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
  LinkedMockStream(AeContext const& context, bool fail_write)
      : context_{context},
        fail_write_{fail_write},
        stream_info_{512, 1024, true, LinkState::kLinked, true},
        out_data_event_{context_},
        stream_update_event_{context_} {}

  WriteAction& Write(DataBuffer&& /*data*/) override {
    if (on_write_hook_) {
      on_write_hook_();
    }
    return SelectWriteAction(context_, fail_write_, ok_action_, fail_action_);
  }
  StreamUpdateEvent const& stream_update_event() override {
    return stream_update_event_;
  }
  StreamInfo stream_info() const override { return stream_info_; }
  OutDataEvent const& out_data_event() override { return out_data_event_; }
  void Restream() override {}

  void SimulateInbound(DataBuffer data) { out_data_event_.Emit(std::move(data)); }

  void SetWriteHook(std::function<void()> hook) {
    on_write_hook_ = std::move(hook);
  }

 private:
  AeContext context_;
  bool fail_write_;
  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;
  std::optional<ImmediateWriteAction> ok_action_;
  std::optional<FailWriteAction> fail_action_;
  std::function<void()> on_write_hook_;
};

struct FakeBuildPolicy {
  AeContext const* context{nullptr};
  ServerId server_id{};
  bool fail_write{false};
  std::function<void(ServerId, LinkedMockStream*)> on_stream_built{};
};

class PingPushAckFakeChannel final : public Channel {
  AE_OBJECT(PingPushAckFakeChannel, Channel, 2)

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
    auto stream = std::make_unique<LinkedMockStream>(*policy_.context,
                                                    policy_.fail_write);
    if (policy_.on_stream_built) {
      policy_.on_stream_built(policy_.server_id, stream.get());
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

  explicit PingHarness(bool fail_write = false, bool connect_primary = true)
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
    policy.on_stream_built = [this](ServerId id, LinkedMockStream* stream) {
      streams_by_server_[id] = stream;
    };
    ready = AttachLinkedServer(kServerId) && AttachLinkedServer(kServerIdB);
    if (connect_primary) {
      ready = ready && ConnectCloud(kServerId);
    }
  }

  LinkedMockStream* Stream(ServerId id) {
    auto it = streams_by_server_.find(id);
    return it == streams_by_server_.end() ? nullptr : it->second;
  }

  bool AttachLinkedServer(ServerId id) {
    auto server = aether->GetServer(id).Load();
    if (server == nullptr) {
      return false;
    }
    FakeBuildPolicy channel_policy = policy;
    channel_policy.server_id = id;
    server->channels = {PingPushAckFakeChannel::ptr::Create(
        CreateWith{domain}, channel_policy)};
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
    ctx.Advance(std::chrono::milliseconds{200});
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
  std::map<ServerId, LinkedMockStream*> streams_by_server_;
  std::unique_ptr<LinkedClientFactory> factory;
  std::unique_ptr<CloudServerConnection> cloud_conn;
};

class InboundSafeApiEncryptKeyProvider final : public ISyncKeyProvider {
 public:
  InboundSafeApiEncryptKeyProvider(ObjPtr<Client> client, ServerId server_id)
      : client_{client}, server_id_{server_id} {}

  Key GetKey() const override {
    auto client_ptr = client_.Load();
    assert(client_ptr);
    auto* server_key = client_ptr->server_state(server_id_);
    assert(server_key);
    return server_key->server_to_client();
  }

  CryptoNonce const& Nonce() const override {
    auto client_ptr = client_.Load();
    assert(client_ptr);
    auto* server_key = client_ptr->server_state(server_id_);
    assert(server_key);
    server_key->Next();
    return server_key->nonce();
  }

 private:
  ObjPtr<Client> client_;
  ServerId server_id_;
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

DataBuffer MakeSerializedPingAckPacket() {
  DataBuffer packet;
  ApiPacker packer{packet};
  packer.Pack(kPingAckMessageId, GenericMessage<>{});
  return packet;
}

DataBuffer PackSendSafeApiEncrypted(ObjPtr<Client> const& client,
                                    ServerId server_id,
                                    DataBuffer const& safe_api_plaintext) {
  SyncEncryptProvider encryptor{
      std::make_unique<InboundSafeApiEncryptKeyProvider>(client, server_id)};
  DataBuffer const encrypted = encryptor.Encrypt(safe_api_plaintext);
  DataBuffer outer;
  ApiPacker packer{outer};
  packer.Pack(kSendSafeApiMessageId, encrypted);
  return outer;
}

void DeliverPingAckViaStream(PingHarness& h, ServerId server_id) {
  auto* stream = h.Stream(server_id);
  TEST_ASSERT_NOT_NULL(stream);
  stream->SimulateInbound(
      PackSendSafeApiEncrypted(h.client, server_id, MakeSerializedPingAckPacket()));
}

std::unique_ptr<Ping> MakePing(PingHarness& h) {
  return std::unique_ptr<Ping>(new Ping(
      h.ae_ctx, h.CloudConn(), PingHarness::kHint, PingHarness::kRxWindow,
      PingHarness::kPingTimeout));
}

void StartPing(PingHarness& h, Ping& ping, ResultCapture& cap) {
  cap.Attach(ping, h.ctx.event_system());
  auto const start = h.ctx.Now();
  ping.Start(start);
  h.ctx.Advance(std::chrono::milliseconds{20});
}

}  // namespace

void test_SchedulerDelayedTaskFiresOnceThroughAeContext() {
  TestContext ctx;
  AeContext ae_ctx{ctx};
  int calls{0};
  TaskSubscription sub;
  auto const start = ctx.Now();
  sub = ae_ctx.scheduler().DelayedTask([&]() { ++calls; },
                                       start + std::chrono::milliseconds{50});

  ctx.Advance(std::chrono::milliseconds{49});
  TEST_ASSERT_EQUAL(0, calls);

  ctx.Advance(std::chrono::milliseconds{2});
  TEST_ASSERT_EQUAL(1, calls);

  ctx.Advance(std::chrono::milliseconds{100});
  TEST_ASSERT_EQUAL(1, calls);
}

void test_PushPingAckCompletesPingBeforeTimeout() {
  PingHarness h;
  TEST_ASSERT_TRUE(h.ready);
  auto ping = MakePing(h);
  ResultCapture cap;
  StartPing(h, *ping, cap);

  DeliverPingAckViaStream(h, kServerId);
  h.ctx.Advance(std::chrono::milliseconds{30});

  TEST_ASSERT_EQUAL(1, cap.count);
  TEST_ASSERT_TRUE(cap.HasOk());

  auto const deadline =
      h.ctx.Now() + PingHarness::kPingTimeout + std::chrono::milliseconds{50};
  h.ctx.AdvanceUntil(deadline);
  TEST_ASSERT_EQUAL(1, cap.count);
  TEST_ASSERT_TRUE(cap.HasOk());
}

void test_FastPingAckDuringTransportWriteBeforeStartReturns() {
  PingHarness h;
  TEST_ASSERT_TRUE(h.ready);
  auto ping = MakePing(h);
  ResultCapture cap;
  cap.Attach(*ping, h.ctx.event_system());

  auto* stream = h.Stream(kServerId);
  TEST_ASSERT_NOT_NULL(stream);
  stream->SetWriteHook([&]() { DeliverPingAckViaStream(h, kServerId); });

  ping->Start(h.ctx.Now());
  h.ctx.Advance(std::chrono::milliseconds{150});

  TEST_ASSERT_EQUAL(1, cap.count);
  TEST_ASSERT_TRUE(cap.HasOk());
}

void test_MissingAckTimesOutOnce() {
  PingHarness h;
  TEST_ASSERT_TRUE(h.ready);
  auto ping = MakePing(h);
  ResultCapture cap;
  StartPing(h, *ping, cap);
  TEST_ASSERT_TRUE(ping->IsPending());
  h.ctx.Advance(PingHarness::kPingTimeout + std::chrono::milliseconds{30});
  TEST_ASSERT_EQUAL(1, cap.count);
  TEST_ASSERT_TRUE(cap.HasTimeout());
  ping.reset();
}

void test_LateAckAfterTimeoutEmitsLateDuration() {
  PingHarness h;
  TEST_ASSERT_TRUE(h.ready);
  auto ping = MakePing(h);
  ResultCapture cap;
  cap.Attach(*ping, h.ctx.event_system());
  auto const start = h.ctx.Now();
  ping->Start(start);
  h.ctx.Advance(std::chrono::milliseconds{20});
  h.ctx.AdvanceUntil(start + PingHarness::kPingTimeout +
                     std::chrono::milliseconds{20});
  TEST_ASSERT_EQUAL(1, cap.count);
  TEST_ASSERT_TRUE(cap.HasTimeout());

  DeliverPingAckViaStream(h, kServerId);
  h.ctx.Advance(std::chrono::milliseconds{20});
  TEST_ASSERT_EQUAL(2, cap.count);
  TEST_ASSERT_TRUE(cap.HasLate());
}

void test_DuplicateAckAfterSuccessIgnored() {
  PingHarness h;
  TEST_ASSERT_TRUE(h.ready);
  auto ping = MakePing(h);
  ResultCapture cap;
  StartPing(h, *ping, cap);
  DeliverPingAckViaStream(h, kServerId);
  DeliverPingAckViaStream(h, kServerId);
  h.ctx.Advance(std::chrono::milliseconds{150});
  TEST_ASSERT_EQUAL(1, cap.count);
  TEST_ASSERT_TRUE(cap.HasOk());
}

void test_OtherConnectionAckBeforeStartThenCorrectAckOk() {
  PingHarness h;
  TEST_ASSERT_TRUE(h.ready);

  LinkedClientFactory other_factory(h.ae_ctx, h.client, h.policy);
  CloudServerConnection other_cloud{h.client->cloud().Load(), kServerIdB,
                                    other_factory};
  TEST_ASSERT_TRUE(other_cloud.Connect());
  h.ctx.Advance(std::chrono::milliseconds{200});
  TEST_ASSERT_NOT_NULL(other_cloud.client_connection());

  auto ping = MakePing(h);
  ResultCapture cap;
  cap.Attach(*ping, h.ctx.event_system());
  auto const start = h.ctx.Now();
  ping->Start(start);
  h.ctx.Advance(std::chrono::milliseconds{20});

  DeliverPingAckViaStream(h, kServerIdB);
  h.ctx.Advance(std::chrono::milliseconds{30});
  TEST_ASSERT_EQUAL(0, cap.count);

  DeliverPingAckViaStream(h, kServerId);
  h.ctx.Advance(std::chrono::milliseconds{30});
  TEST_ASSERT_EQUAL(1, cap.count);
  TEST_ASSERT_TRUE(cap.HasOk());
}

void test_WriteFailureThenAckAndDeadlineIgnored() {
  PingHarness h{true};
  TEST_ASSERT_TRUE(h.ready);
  auto ping = MakePing(h);
  ResultCapture cap;
  StartPing(h, *ping, cap);
  h.ctx.Advance(std::chrono::milliseconds{150});
  TEST_ASSERT_EQUAL(1, cap.count);
  TEST_ASSERT_TRUE(cap.HasWriteFail());

  DeliverPingAckViaStream(h, kServerId);
  auto const deadline =
      h.ctx.Now() + PingHarness::kPingTimeout + std::chrono::milliseconds{50};
  h.ctx.AdvanceUntil(deadline);
  TEST_ASSERT_EQUAL(1, cap.count);
  TEST_ASSERT_TRUE(cap.HasWriteFail());
}

void test_DestroyPendingPingThenAckAndDeadlineNoCallbacks() {
  PingHarness h;
  TEST_ASSERT_TRUE(h.ready);
  auto ping = MakePing(h);
  ResultCapture cap;
  cap.Attach(*ping, h.ctx.event_system());
  auto const start = h.ctx.Now();
  ping->Start(start);
  h.ctx.Advance(std::chrono::milliseconds{20});
  ping.reset();

  DeliverPingAckViaStream(h, kServerId);
  h.ctx.AdvanceUntil(start + PingHarness::kPingTimeout +
                     std::chrono::milliseconds{50});
  TEST_ASSERT_EQUAL(0, cap.count);
}

void test_WriteFailDeferredEmitsOnNextSchedulerTick() {
  PingHarness h{true};
  TEST_ASSERT_TRUE(h.ready);
  auto ping = MakePing(h);
  ResultCapture cap;
  cap.Attach(*ping, h.ctx.event_system());
  ping->Start(h.ctx.Now());
  h.ctx.Advance(std::chrono::milliseconds{1});
  TEST_ASSERT_TRUE(ping->IsPending());
  TEST_ASSERT_EQUAL(0, cap.count);
  h.ctx.Advance(std::chrono::milliseconds{1});
  TEST_ASSERT_EQUAL(1, cap.count);
  TEST_ASSERT_TRUE(cap.HasWriteFail());
}

void test_DestroyPingAfterWriteFailDeferredBeforeSchedulerTick() {
  PingHarness h{true};
  TEST_ASSERT_TRUE(h.ready);
  auto ping = MakePing(h);
  ResultCapture cap;
  cap.Attach(*ping, h.ctx.event_system());
  ping->Start(h.ctx.Now());
  h.ctx.Advance(std::chrono::milliseconds{1});
  TEST_ASSERT_TRUE(ping->IsPending());
  TEST_ASSERT_EQUAL(0, cap.count);
  ping.reset();
  h.ctx.Advance(std::chrono::milliseconds{200});
  TEST_ASSERT_EQUAL(0, cap.count);
}

namespace {

int RunPingPushAckTestsOnce() {
  UNITY_BEGIN();

  RUN_TEST(test_SchedulerDelayedTaskFiresOnceThroughAeContext);
  RUN_TEST(test_PushPingAckCompletesPingBeforeTimeout);
  RUN_TEST(test_FastPingAckDuringTransportWriteBeforeStartReturns);
  RUN_TEST(test_MissingAckTimesOutOnce);
  RUN_TEST(test_LateAckAfterTimeoutEmitsLateDuration);
  RUN_TEST(test_DuplicateAckAfterSuccessIgnored);
  RUN_TEST(test_OtherConnectionAckBeforeStartThenCorrectAckOk);
  RUN_TEST(test_WriteFailureThenAckAndDeadlineIgnored);
  RUN_TEST(test_DestroyPendingPingThenAckAndDeadlineNoCallbacks);
  RUN_TEST(test_WriteFailDeferredEmitsOnNextSchedulerTick);
  RUN_TEST(test_DestroyPingAfterWriteFailDeferredBeforeSchedulerTick);

  return UNITY_END();
}

}  // namespace

}  // namespace ae::test_ping_push_ack

extern int test_ping_server_defer_run();
extern int test_ping_timeout_recovery_run();
extern int test_ping_serverping_lifecycle_run();

int test_api_protocol_ping_push_ack() {
  using namespace ae::test_ping_push_ack;
  int failures = RunPingPushAckTestsOnce();
  if (failures == 0) {
    failures = RunPingPushAckTestsOnce();
  }
  if (failures == 0) {
    failures = test_ping_server_defer_run();
  }
  if (failures == 0) {
    failures = test_ping_timeout_recovery_run();
  }
  if (failures == 0) {
    failures = test_ping_serverping_lifecycle_run();
  }
  return failures;
}

#else
int test_api_protocol_ping_push_ack() { return 0; }
#endif  // AE_ENABLE_PING
