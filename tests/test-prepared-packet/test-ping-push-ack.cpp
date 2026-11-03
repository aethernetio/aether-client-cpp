/*
 * Copyright 2026 Aethernet Inc.
 * Push-model PingAck regression (uses stable prepared-packet client harness).
 */

#include <chrono>
#include <cstdio>
#include <memory>
#include <optional>

#include <unity.h>

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "aether/adapter_registry.h"
#include "aether/ae_actions/ping.h"
#include "aether/aether.h"
#include "aether/api_protocol/api_protocol.h"
#include "aether/channels/channel.h"
#include "aether/client.h"
#include "aether/cloud_connections/cloud_server_connection.h"
#include "aether/crypto/key_gen.h"
#include "aether/diagnostics/bench_ping_server_start_defer.h"
#include "aether/env.h"
#include "aether/executors/executors.h"
#include "aether/server.h"
#include "aether/socket_initializer.h"
#include "aether/server_connections/client_server_connection.h"
#include "aether/server_connections/iserver_connection_factory.h"
#include "aether/stream_api/istream.h"
#include "aether/tasks/details/task_subsctiption.h"
#include "aether/tasks/manual_task_scheduler.h"
#include "aether/types/address.h"
#include "aether/write_action/write_action.h"

namespace ae::test_prepared_packet::ping_push_ack {
namespace {

SocketInitializer g_socket_initializer;

constexpr ServerId kPingServerId{42};
constexpr ServerId kOtherServerId{43};
constexpr MessageId kPingAckMessageId{24};

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
  Scheduler sched;
  EventSystem event_system;
};

class ImmediateWriteAction : public WriteAction {
 public:
  explicit ImmediateWriteAction(AeContext const& c) : WriteAction{c} {
    c.scheduler().Task([&] { SetStatus(Status::kSuccess); });
  }
};

class FailWriteAction : public WriteAction {
 public:
  explicit FailWriteAction(AeContext const& c) : WriteAction{c} {
    c.scheduler().Task([&] { SetStatus(Status::kFail); });
  }
};

class LinkedMockStream final : public ByteIStream {
 public:
  explicit LinkedMockStream(AeContext const& c, bool fail_write)
      : ctx_{c},
        fail_write_{fail_write},
        info_{512, 1024, true, LinkState::kLinked, true},
        out_{c},
        upd_{c} {}
  WriteAction& Write(DataBuffer&&) override {
    if (fail_write_) {
      return fail_.emplace(ctx_);
    }
    return ok_.emplace(ctx_);
  }
  StreamUpdateEvent const& stream_update_event() override { return upd_; }
  StreamInfo stream_info() const override { return info_; }
  OutDataEvent const& out_data_event() override { return out_; }
  void Restream() override {}

 private:
  AeContext ctx_;
  bool fail_write_;
  StreamInfo info_;
  OutDataEvent out_;
  StreamUpdateEvent upd_;
  std::optional<ImmediateWriteAction> ok_;
  std::optional<FailWriteAction> fail_;
};

struct ChannelPolicy {
  AeContext const* ctx{nullptr};
  bool fail_write{false};
};

class PingAckFakeChannel final : public Channel {
  AE_OBJECT(PingAckFakeChannel, Channel, 0)

 protected:
  PingAckFakeChannel() = default;

 public:
  PingAckFakeChannel(ObjProp prop, ChannelPolicy policy)
      : Channel{prop}, policy_{policy} {
    transport_properties_.max_packet_size = 1024;
    transport_properties_.rec_packet_size = 512;
    transport_properties_.connection_type = ConnectionType::kConnectionFull;
    transport_properties_.reliability = Reliability::kReliable;
  }
  AE_OBJECT_REFLECT()
  TransportBuildSender TransportBuilder() override {
    return ex::just(std::unique_ptr<ByteIStream>{std::make_unique<LinkedMockStream>(
        *policy_.ctx, policy_.fail_write)});
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

struct Harness {
  static constexpr Duration kTimeout{std::chrono::milliseconds{100}};

  explicit Harness(bool fail_write = false) : ae{ctx}, domain{storage, &ctx} {
    policy.ctx = &ae;
    policy.fail_write = fail_write;
    aether = Aether::ptr::Create(CreateWith{domain});
    registry = AdapterRegistry::ptr::Create(CreateWith{domain});
    aether->adapter_registry = registry;
    ctx.SetAether(aether);
    aether->client_prefab = Client::ptr::Create(CreateWith{domain}, aether);
    aether->client_prefab.Save();
    Key mk{};
    CryptoSyncKeygen(mk);
    auto ep1 = Endpoint{{IpV4Addr{{192, 0, 2, 1}}, 9021}, Protocol::kUdp};
    auto ep2 = Endpoint{{IpV4Addr{{192, 0, 2, 2}}, 9022}, Protocol::kUdp};
    ClientConfig cfg{.parent_uid = Uid{{1}},
                     .uid = Uid{{2}},
                     .ephemeral_uid = Uid{{3}},
                     .master_key = std::move(mk),
                     .cloud = {{kPingServerId, {ep1}}, {kOtherServerId, {ep2}}}};
    client = aether->CreateClient(cfg, "ping-ack");
    for (auto sid : {kPingServerId, kOtherServerId}) {
      auto s = aether->GetServer(sid).Load();
      s->channels = {PingAckFakeChannel::ptr::Create(CreateWith{domain}, policy)};
    }
    factory = std::make_unique<Factory>(ae, client, policy);
    cloud = std::make_unique<CloudServerConnection>(client->cloud().Load(),
                                                    kPingServerId, *factory);
    ready = cloud->Connect();
    ctx.PumpFor(std::chrono::milliseconds{200});
    ready = ready && cloud->client_connection() != nullptr &&
            cloud->client_connection()->stream_info().link_state ==
                LinkState::kLinked;
  }

  TestContext ctx;
  RamDomainStorage storage;
  Domain domain;
  AeContext ae;
  Aether::ptr aether;
  AdapterRegistry::ptr registry;
  ObjPtr<Client> client;
  ChannelPolicy policy;
  std::unique_ptr<Factory> factory;
  std::unique_ptr<CloudServerConnection> cloud;
  bool ready{false};
};

DataBuffer SerializedPingAckSafeApi() {
  DataBuffer buf;
  ApiPacker p{buf};
  p.Pack(kPingAckMessageId, GenericMessage<>{});
  return buf;
}

void DeliverPingAck(ClientServerConnection& cc) {
  ClientServerConnectionTestAccess::DeliverInboundSafeApiPlaintext(
      cc, SerializedPingAckSafeApi());
}

struct Cap {
  int n{0};
  std::optional<Ping::PingResult> last;
  Subscription sub;
  void Bind(Ping const& ping) {
    sub = ping.result_event().Subscribe([this](Ping::PingResult const& r) {
      ++n;
      last = r;
    });
  }
  bool ok() const {
    return last && std::holds_alternative<Ok<Duration>>(*last);
  }
  bool timeout() const {
    return last && std::holds_alternative<Error<int>>(*last) &&
           std::get<Error<int>>(*last).error == 2;
  }
  bool write_fail() const {
    return last && std::holds_alternative<Error<int>>(*last) &&
           std::get<Error<int>>(*last).error == 1;
  }
  bool late() const {
    return last && std::holds_alternative<Ping::LateDuration>(*last);
  }
};

std::unique_ptr<Ping> MakePing(Harness& h) {
  return std::unique_ptr<Ping>(new Ping(
      h.ae, *h.cloud, std::chrono::seconds{1}, std::chrono::milliseconds{5},
      Harness::kTimeout, Harness::kTimeout));
}

void StartPing(Harness& h, Ping& ping, Cap& cap) {
  cap.Bind(ping);
  ping.Start(std::chrono::system_clock::now());
  h.ctx.PumpFor(std::chrono::milliseconds{20});
}

}  // namespace

void test_PushPingAckCompletesBeforeTimeout() {
  Harness h;
  TEST_ASSERT_TRUE(h.ready);
  auto ping = MakePing(h);
  Cap cap;
  StartPing(h, *ping, cap);
  DeliverPingAck(*h.cloud->client_connection());
  h.ctx.PumpFor(Harness::kTimeout + std::chrono::milliseconds{50});
  TEST_ASSERT_EQUAL(1, cap.n);
  TEST_ASSERT_TRUE(cap.ok());
}

void test_FastPingAckDuringSendIsNotLost() {
  Harness h;
  TEST_ASSERT_TRUE(h.ready);
  auto ping = MakePing(h);
  Cap cap;
  cap.Bind(*ping);
  ping->Start(std::chrono::system_clock::now());
  DeliverPingAck(*h.cloud->client_connection());
  h.ctx.PumpFor(std::chrono::milliseconds{150});
  TEST_ASSERT_EQUAL(1, cap.n);
  TEST_ASSERT_TRUE(cap.ok());
}

void test_MissingAckTimesOut() {
  Harness h;
  TEST_ASSERT_TRUE(h.ready);
  auto ping = MakePing(h);
  Cap cap;
  cap.Bind(*ping);
  ping->Start(std::chrono::system_clock::now());
  h.ctx.PumpFor(Harness::kTimeout + std::chrono::milliseconds{50});
  TEST_ASSERT_EQUAL(1, cap.n);
  TEST_ASSERT_TRUE(cap.timeout());
}

void test_LateAckAfterTimeout() {
  Harness h;
  TEST_ASSERT_TRUE(h.ready);
  auto ping = MakePing(h);
  Cap cap;
  cap.Bind(*ping);
  ping->Start(std::chrono::system_clock::now());
  h.ctx.PumpFor(Harness::kTimeout + std::chrono::milliseconds{20});
  TEST_ASSERT_TRUE(cap.timeout());
  DeliverPingAck(*h.cloud->client_connection());
  h.ctx.PumpFor(std::chrono::milliseconds{20});
  TEST_ASSERT_EQUAL(2, cap.n);
  TEST_ASSERT_TRUE(cap.late());
}

void test_DuplicateAckAfterSuccessIgnored() {
  Harness h;
  TEST_ASSERT_TRUE(h.ready);
  auto ping = MakePing(h);
  Cap cap;
  StartPing(h, *ping, cap);
  DeliverPingAck(*h.cloud->client_connection());
  DeliverPingAck(*h.cloud->client_connection());
  h.ctx.PumpFor(std::chrono::milliseconds{150});
  TEST_ASSERT_EQUAL(1, cap.n);
  TEST_ASSERT_TRUE(cap.ok());
}

void test_AckOnOtherConnectionDoesNotCompletePing() {
  Harness h;
  TEST_ASSERT_TRUE(h.ready);
  auto ping = MakePing(h);
  Cap cap;
  StartPing(h, *ping, cap);
  Factory other_factory(h.ae, h.client, h.policy);
  CloudServerConnection other_cloud{h.client->cloud().Load(), kOtherServerId,
                                    other_factory};
  TEST_ASSERT_TRUE(other_cloud.Connect());
  h.ctx.PumpFor(std::chrono::milliseconds{200});
  auto* other_cc = other_cloud.client_connection();
  TEST_ASSERT_NOT_NULL(other_cc);
  DeliverPingAck(*other_cc);
  h.ctx.PumpFor(Harness::kTimeout + std::chrono::milliseconds{30});
  TEST_ASSERT_EQUAL(1, cap.n);
  TEST_ASSERT_TRUE(cap.timeout());
}

void test_WriteFailureReleasesSubscriptions() {
  Harness h{true};
  TEST_ASSERT_TRUE(h.ready);
  auto ping = MakePing(h);
  Cap cap;
  StartPing(h, *ping, cap);
  h.ctx.PumpFor(std::chrono::milliseconds{150});
  TEST_ASSERT_EQUAL(1, cap.n);
  TEST_ASSERT_TRUE(cap.write_fail());
  DeliverPingAck(*h.cloud->client_connection());
  h.ctx.PumpFor(std::chrono::milliseconds{150});
  TEST_ASSERT_EQUAL(1, cap.n);
}

void test_DestroyPendingPingDoesNotCrash() {
  Harness h;
  TEST_ASSERT_TRUE(h.ready);
  auto ping = MakePing(h);
  Cap cap;
  cap.Bind(*ping);
  ping->Start(std::chrono::system_clock::now());
  h.ctx.PumpFor(std::chrono::milliseconds{10});
  ping.reset();
  h.ctx.PumpFor(std::chrono::milliseconds{150});
  TEST_ASSERT_TRUE(true);
}

void test_DeferPeriodicWhilePending() {
#if AE_BENCH_PING_MIN_TIMEOUT_MS > 0
  bench_ping_server_start::DeferState st{};
  TEST_ASSERT_TRUE(
      bench_ping_server_start::TryDeferPeriodicStart(st, true, true));
  TEST_ASSERT_TRUE(st.deferred_start_after_ping);
  TEST_ASSERT_FALSE(
      bench_ping_server_start::TryDeferPeriodicStart(st, true, false));
#else
  TEST_MESSAGE("AE_BENCH_PING_MIN_TIMEOUT_MS=0 in this build; defer not compiled");
#endif
}

}  // namespace ae::test_prepared_packet::ping_push_ack

int test_prepared_packet_ping_push_ack() {
  UNITY_BEGIN();
  using namespace ae::test_prepared_packet::ping_push_ack;
  RUN_TEST(test_DeferPeriodicWhilePending);
  RUN_TEST(test_PushPingAckCompletesBeforeTimeout);
  RUN_TEST(test_FastPingAckDuringSendIsNotLost);
  RUN_TEST(test_MissingAckTimesOut);
  RUN_TEST(test_LateAckAfterTimeout);
  RUN_TEST(test_DuplicateAckAfterSuccessIgnored);
  RUN_TEST(test_AckOnOtherConnectionDoesNotCompletePing);
  RUN_TEST(test_WriteFailureReleasesSubscriptions);
  RUN_TEST(test_DestroyPendingPingDoesNotCrash);
  return UNITY_END();
}
