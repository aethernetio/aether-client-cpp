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

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <unity.h>

#include "aether/adapter_registry.h"
#include "aether/ae_context.h"
#include "aether/channels/channel.h"
#include "aether/config.h"
#include "aether/executors/executors.h"
#include "aether/obj/domain.h"
#include "aether/server.h"
#include "aether/server_connections/server_connection.h"
#include "aether/stream_api/istream.h"
#include "aether/tasks/details/task_subsctiption.h"
#include "aether/types/address.h"
#include "aether/types/server_id.h"
#include "aether/write_action/write_action.h"

#include "tests/test-object-system/map_domain_storage.h"

namespace ae {

struct ServerConnectionTestAccess {
  static std::size_t ChannelCount(ServerConnection const& c) {
    return c.channels_.size();
  }
  static bool ChannelFailed(ServerConnection const& c, std::size_t index) {
    return c.channels_.at(index)->failed;
  }
  static std::size_t FailedCount(ServerConnection const& c) {
    std::size_t n = 0;
    for (auto const& entry : c.channels_) {
      if (entry->failed) {
        ++n;
      }
    }
    return n;
  }
  static bool SelectActionFinished(ServerConnection const& c) {
    return !c.channel_select_action_ ||
           c.channel_select_action_->is_finished();
  }
  static bool SelectActionFinishedFlag(ServerConnection const& c) {
    return c.channel_select_action_ &&
           c.channel_select_action_->is_finished();
  }
};

namespace {

struct TestContext {
  AeCtx ToAeContext() const {
    static constexpr auto table =
        AeCtxTable{nullptr, [](void* obj) -> TaskScheduler& {
                     return static_cast<TestContext*>(obj)->sched;
                   }};
    return AeCtx{const_cast<TestContext*>(this), &table};  // NOLINT
  }

  void Pump(int rounds = 64) {
    auto now = std::chrono::system_clock::now();
    for (int i = 0; i < rounds; ++i) {
      now = sched.Update(now);
      if (now == std::chrono::system_clock::time_point::max()) {
        now = std::chrono::system_clock::now();
      }
    }
  }

  TaskScheduler sched;
};

class ImmediateWriteAction : public WriteAction {
 public:
  explicit ImmediateWriteAction(AeContext const& context) {
    context.scheduler().Task(
        [&]() { WriteAction::SetStatus(Status::kSuccess); });
  }
};

class LinkedMockStream final : public ByteIStream {
 public:
  explicit LinkedMockStream(AeContext const& context)
      : context_{context},
        stream_info_{512, 1024, true, LinkState::kLinked, true} {}

  WriteAction& Write(DataBuffer&& /*data*/) override {
    return last_action_.emplace(context_);
  }
  StreamUpdateEvent::Subscriber stream_update_event() override {
    return EventSubscriber{stream_update_event_};
  }
  StreamInfo stream_info() const override { return stream_info_; }
  OutDataEvent::Subscriber out_data_event() override {
    return EventSubscriber{out_data_event_};
  }
  void Restream() override {}

 private:
  AeContext context_;
  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;
  std::optional<ImmediateWriteAction> last_action_;
};

struct FakeBuildPolicy {
  enum class Mode : std::uint8_t { kAlwaysFail, kAlwaysSucceed, kFailThenSucceed };
  Mode mode{Mode::kAlwaysFail};
  int* builds{nullptr};
  AeContext const* context{nullptr};
};

class FakeChannel final : public Channel {
  AE_OBJECT(FakeChannel, Channel, 0)

 protected:
  FakeChannel() = default;

 public:
  FakeChannel(ObjProp prop, FakeBuildPolicy policy)
      : Channel{prop}, policy_{policy} {
    transport_properties_.max_packet_size = 1024;
    transport_properties_.rec_packet_size = 512;
    transport_properties_.connection_type = ConnectionType::kConnectionFull;
    transport_properties_.reliability = Reliability::kReliable;
  }

  AE_OBJECT_REFLECT()

  TransportBuildSender TransportBuilder() override {
    if (policy_.builds != nullptr) {
      ++(*policy_.builds);
    }
    auto const builds = policy_.builds != nullptr ? *policy_.builds : 1;
    bool succeed = false;
    switch (policy_.mode) {
      case FakeBuildPolicy::Mode::kAlwaysSucceed:
        succeed = true;
        break;
      case FakeBuildPolicy::Mode::kFailThenSucceed:
        succeed = builds > 1;
        break;
      case FakeBuildPolicy::Mode::kAlwaysFail:
      default:
        succeed = false;
        break;
    }
    if (!succeed) {
      return ex::just_error(1);
    }
    assert(policy_.context != nullptr);
    return ex::just(std::unique_ptr<ByteIStream>{
        std::make_unique<LinkedMockStream>(*policy_.context)});
  }

  Duration TransportBuildTimeout() const override {
    return std::chrono::milliseconds{50};
  }
  Duration ResponseTimeout() const override {
    return std::chrono::milliseconds{50};
  }

 private:
  FakeBuildPolicy policy_;
};

Server::ptr MakeServerWithChannels(Domain& domain,
                                   std::vector<FakeChannel::ptr> channels) {
  auto registry = AdapterRegistry::ptr::Create(CreateWith{domain});
  auto server = Server::ptr::Create(CreateWith{domain}, ServerId{1},
                                    std::vector<Endpoint>{}, registry);
  for (auto& channel : channels) {
    server->channels.emplace_back(channel);
  }
  return server;
}

void test_single_channel_build_failure_reaches_link_error() {
  TestContext ctx;
  AeContext ae_ctx{ctx};
  MapDomainStorage storage;
  Domain domain{Now(), storage};

  int builds = 0;
  FakeBuildPolicy policy{FakeBuildPolicy::Mode::kAlwaysFail, &builds, &ae_ctx};
  auto channel = FakeChannel::ptr::Create(CreateWith{domain}, policy);
  auto server = MakeServerWithChannels(domain, {channel});

  int server_errors = 0;
  LinkState last_state = LinkState::kUnlinked;
  bool last_writable = true;
  bool finished_during_result = false;

  ServerConnection connection{ae_ctx, server.Load()};
  auto err_sub =
      connection.server_error_event().Subscribe([&]() { ++server_errors; });
  auto upd_sub = connection.stream_update_event().Subscribe([&]() {
    last_state = connection.stream_info().link_state;
    last_writable = connection.stream_info().is_writable;
  });
  // Re-subscribe is not available after construction; verify finished flag
  // after pump and via access helper during ChannelBuildFailed path.
  finished_during_result =
      ServerConnectionTestAccess::SelectActionFinished(connection);

  ctx.Pump();

  TEST_ASSERT_TRUE(ServerConnectionTestAccess::SelectActionFinished(connection));
  TEST_ASSERT_EQUAL_UINT(1, ServerConnectionTestAccess::FailedCount(connection));
  TEST_ASSERT_TRUE(ServerConnectionTestAccess::ChannelFailed(connection, 0));
  TEST_ASSERT_EQUAL(static_cast<int>(LinkState::kLinkError),
                    static_cast<int>(connection.stream_info().link_state));
  TEST_ASSERT_FALSE(connection.stream_info().is_writable);
  TEST_ASSERT_EQUAL(1, server_errors);
  TEST_ASSERT_EQUAL(static_cast<int>(LinkState::kLinkError),
                    static_cast<int>(last_state));
  TEST_ASSERT_FALSE(last_writable);
  TEST_ASSERT_EQUAL(1, builds);
  (void)finished_during_result;
}

void test_result_callback_sees_finished_action() {
  TestContext ctx;
  AeContext ae_ctx{ctx};
  MapDomainStorage storage;
  Domain domain{Now(), storage};

  int builds = 0;
  FakeBuildPolicy policy{FakeBuildPolicy::Mode::kAlwaysFail, &builds, &ae_ctx};
  auto channel = FakeChannel::ptr::Create(CreateWith{domain}, policy);
  auto server = MakeServerWithChannels(domain, {channel});

  ServerConnection connection{ae_ctx, server.Load()};
  TEST_ASSERT_TRUE_MESSAGE(
      ServerConnectionTestAccess::SelectActionFinishedFlag(connection),
      "sync failure must finish action before pump");
  ctx.Pump(16);
  TEST_ASSERT_EQUAL(static_cast<int>(LinkState::kLinkError),
                    static_cast<int>(connection.stream_info().link_state));
}

void test_second_channel_succeeds_after_first_failure() {
  TestContext ctx;
  AeContext ae_ctx{ctx};
  MapDomainStorage storage;
  Domain domain{Now(), storage};

  int builds = 0;
  FakeBuildPolicy policy{FakeBuildPolicy::Mode::kFailThenSucceed, &builds,
                         &ae_ctx};
  auto ch0 = FakeChannel::ptr::Create(CreateWith{domain}, policy);
  auto ch1 = FakeChannel::ptr::Create(CreateWith{domain}, policy);
  auto server = MakeServerWithChannels(domain, {ch0, ch1});

  ServerConnection connection{ae_ctx, server.Load()};
  ctx.Pump();

  TEST_ASSERT_TRUE(ServerConnectionTestAccess::SelectActionFinished(connection));
  TEST_ASSERT_EQUAL_UINT(1, ServerConnectionTestAccess::FailedCount(connection));
  TEST_ASSERT_EQUAL(static_cast<int>(LinkState::kLinked),
                    static_cast<int>(connection.stream_info().link_state));
  TEST_ASSERT_TRUE(connection.stream_info().is_writable);
  TEST_ASSERT_EQUAL(2, builds);
}

void test_full_pool_still_reaches_link_error() {
  TestContext ctx;
  AeContext ae_ctx{ctx};
  MapDomainStorage storage;
  Domain domain{Now(), storage};

  // Leave a few slots for transport timeout plumbing; keep the rest occupied
  // with active delayed tasks so deferred reselect cannot allocate.
  static constexpr auto kBlockers = AE_TASK_MAX_COUNT > 8
                                        ? AE_TASK_MAX_COUNT - 8
                                        : AE_TASK_MAX_COUNT / 2;
  std::array<TaskSubscription, kBlockers> blockers{};
  for (auto& sub : blockers) {
    sub = ctx.sched.DelayedTask([]() {}, std::chrono::seconds{60});
    TEST_ASSERT_TRUE_MESSAGE(static_cast<bool>(sub),
                             "expected to fill scheduler pool");
  }

  int builds = 0;
  FakeBuildPolicy policy{FakeBuildPolicy::Mode::kAlwaysFail, &builds, &ae_ctx};
  auto channel = FakeChannel::ptr::Create(CreateWith{domain}, policy);
  auto server = MakeServerWithChannels(domain, {channel});

  int server_errors = 0;
  ServerConnection connection{ae_ctx, server.Load()};
  auto err_sub =
      connection.server_error_event().Subscribe([&]() { ++server_errors; });

  // Exhaust remaining slots so DeferSelectChannel / DeferServerError fail.
  std::vector<TaskSubscription> extra;
  for (;;) {
    auto sub = ctx.sched.DelayedTask([]() {}, std::chrono::seconds{60});
    if (!sub) {
      break;
    }
    extra.push_back(std::move(sub));
  }

  // Force the deferred path by pumping once if needed; if schedule already
  // failed during ChannelBuildFailed, link error is already set.
  ctx.Pump(8);

  TEST_ASSERT_TRUE(ServerConnectionTestAccess::SelectActionFinished(connection));
  TEST_ASSERT_EQUAL(static_cast<int>(LinkState::kLinkError),
                    static_cast<int>(connection.stream_info().link_state));
  TEST_ASSERT_TRUE_MESSAGE(server_errors >= 1, "expected server error");
  TEST_ASSERT_EQUAL(1, builds);
}

void test_no_busy_loop_on_permanent_failure() {
  TestContext ctx;
  AeContext ae_ctx{ctx};
  MapDomainStorage storage;
  Domain domain{Now(), storage};

  int builds = 0;
  FakeBuildPolicy policy{FakeBuildPolicy::Mode::kAlwaysFail, &builds, &ae_ctx};
  auto channel = FakeChannel::ptr::Create(CreateWith{domain}, policy);
  auto server = MakeServerWithChannels(domain, {channel});

  ServerConnection connection{ae_ctx, server.Load()};
  ctx.Pump(256);

  TEST_ASSERT_EQUAL(1, builds);
  TEST_ASSERT_EQUAL(static_cast<int>(LinkState::kLinkError),
                    static_cast<int>(connection.stream_info().link_state));
}

}  // namespace
}  // namespace ae

int run_test_server_connection_recovery() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_single_channel_build_failure_reaches_link_error);
  RUN_TEST(ae::test_result_callback_sees_finished_action);
  RUN_TEST(ae::test_second_channel_succeeds_after_first_failure);
  RUN_TEST(ae::test_full_pool_still_reaches_link_error);
  RUN_TEST(ae::test_no_busy_loop_on_permanent_failure);
  return UNITY_END();
}
