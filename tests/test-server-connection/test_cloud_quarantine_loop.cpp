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

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <unity.h>

#include "aether/adapter_registry.h"
#include "aether/ae_context.h"
#include "aether/cloud.h"
#include "aether/cloud_connections/cloud_server_connections.h"
#include "aether/config.h"
#include "aether/obj/domain.h"
#include "aether/server.h"
#include "aether/server_connections/client_server_connection.h"
#include "aether/server_connections/iserver_connection_factory.h"
#include "aether/types/address.h"
#include "aether/types/server_id.h"
#include "aether/work_cloud.h"

#include "tests/test-object-system/map_domain_storage.h"

namespace ae {
struct CloudServerConnectionsTestAccess {
  static bool Quarantine(CloudServerConnections& connections,
                         CloudServerConnection& server) {
    return connections.QuarantineServer(server);
  }

  static void Release(CloudServerConnections& connections,
                      CloudServerConnection& server) {
    connections.ReleaseQuarantinedServer(server);
  }

  static bool HasStateSubscription(CloudServerConnections& connections,
                                   CloudServerConnection& server) {
    return static_cast<bool>(connections.ServerEntryFor(server).state_sub);
  }

  static bool HasErrorSubscription(CloudServerConnections& connections,
                                   CloudServerConnection& server) {
    return static_cast<bool>(connections.ServerEntryFor(server).error_sub);
  }

  static bool HasQuarantineTask(CloudServerConnections& connections,
                                CloudServerConnection& server) {
    return static_cast<bool>(connections.ServerEntryFor(server).quarantine_sub);
  }
};

namespace test_cloud_quarantine_loop {
struct TestContext {
  AeCtx ToAeContext() const {
    static constexpr auto table =
        AeCtxTable{nullptr, [](void* obj) -> TaskScheduler& {
                     return static_cast<TestContext*>(obj)->sched;
                   }};
    return AeCtx{const_cast<TestContext*>(this), &table};  // NOLINT
  }

  void PumpAt(std::chrono::system_clock::time_point now, int rounds = 8) {
    for (int i = 0; i < rounds; ++i) {
      (void)sched.Update(now);
    }
  }

  TaskScheduler sched;
};

class CountingNullFactory final : public IServerConnectionFactory {
 public:
  std::shared_ptr<ClientServerConnection> CreateConnection(
      Ptr<Server> const& /*server*/) override {
    ++attempts;
    return {};
  }
  int attempts{0};
};

class SwitchableNullFactory final : public IServerConnectionFactory {
 public:
  std::shared_ptr<ClientServerConnection> CreateConnection(
      Ptr<Server> const& /*server*/) override {
    ++attempts;
    return {};
  }
  bool available{false};
  int attempts{0};
};

struct CloudFixture {
  CloudFixture(std::unique_ptr<IServerConnectionFactory> factory,
               IServerConnectionFactory* raw,
               std::vector<ServerId> additional_server_ids = {},
               std::size_t max_connections = 1)
      : ae_ctx{ctx},
        domain{Now(), storage},
        registry{AdapterRegistry::ptr::Create(CreateWith{domain})},
        server{Server::ptr::Create(CreateWith{domain}, ServerId{7},
                                   std::vector<Endpoint>{}, registry)},
        cloud{Cloud::ptr::Create(CreateWith{domain})},
        factory_raw{raw} {
    cloud->AddServer(server);
    for (auto server_id : additional_server_ids) {
      cloud->AddServer(Server::ptr::Create(CreateWith{domain}, server_id,
                                           std::vector<Endpoint>{}, registry));
    }
    connections = std::make_unique<CloudServerConnections>(
        ae_ctx, cloud.Load(), std::move(factory), max_connections);
  }

  bool AnyQuarantined() const {
    for (auto* s : connections->servers()) {
      if (s->quarantine()) {
        return true;
      }
    }
    return false;
  }

  TestContext ctx;
  AeContext ae_ctx;
  MapDomainStorage storage;
  Domain domain;
  AdapterRegistry::ptr registry;
  Server::ptr server;
  Cloud::ptr cloud;
  IServerConnectionFactory* factory_raw{nullptr};
  std::unique_ptr<CloudServerConnections> connections;
};

void AssertCanonicalPriorities(CloudServerConnections& connections) {
  auto const& servers = connections.servers();
  for (std::size_t i = 0; i < servers.size(); ++i) {
    TEST_ASSERT_EQUAL_UINT(static_cast<unsigned int>(i),
                           static_cast<unsigned int>(servers[i]->priority()));
  }
}

void test_CloudQuarantineAndReleasePreserveCanonicalOrder() {
  auto factory = std::make_unique<CountingNullFactory>();
  auto* factory_raw = factory.get();
  CloudFixture f{
      std::move(factory), factory_raw, {ServerId{8}, ServerId{9}}, /*max*/ 0};
  auto& servers = f.connections->servers();

  TEST_ASSERT_EQUAL_UINT(ServerId{7}, servers[0]->server_id());
  TEST_ASSERT_EQUAL_UINT(ServerId{8}, servers[1]->server_id());
  TEST_ASSERT_EQUAL_UINT(ServerId{9}, servers[2]->server_id());
  AssertCanonicalPriorities(*f.connections);

  TEST_ASSERT_TRUE(CloudServerConnectionsTestAccess::Quarantine(*f.connections,
                                                                *servers[0]));
  TEST_ASSERT_EQUAL_UINT(ServerId{8}, servers[0]->server_id());
  TEST_ASSERT_EQUAL_UINT(ServerId{9}, servers[1]->server_id());
  TEST_ASSERT_EQUAL_UINT(ServerId{7}, servers[2]->server_id());
  AssertCanonicalPriorities(*f.connections);

  TEST_ASSERT_TRUE(CloudServerConnectionsTestAccess::Quarantine(*f.connections,
                                                                *servers[0]));
  TEST_ASSERT_EQUAL_UINT(ServerId{9}, servers[0]->server_id());
  TEST_ASSERT_EQUAL_UINT(ServerId{7}, servers[1]->server_id());
  TEST_ASSERT_EQUAL_UINT(ServerId{8}, servers[2]->server_id());
  AssertCanonicalPriorities(*f.connections);

  CloudServerConnectionsTestAccess::Release(*f.connections, *servers[1]);
  TEST_ASSERT_EQUAL_UINT(ServerId{9}, servers[0]->server_id());
  TEST_ASSERT_EQUAL_UINT(ServerId{7}, servers[1]->server_id());
  TEST_ASSERT_EQUAL_UINT(ServerId{8}, servers[2]->server_id());
  AssertCanonicalPriorities(*f.connections);
}

void test_CloudQuarantineDoesNotBusyLoop() {
  auto factory = std::make_unique<CountingNullFactory>();
  auto* factory_raw = factory.get();
  CloudFixture f{std::move(factory), factory_raw};

  auto t0 = std::chrono::system_clock::now();
  f.ctx.PumpAt(t0, 16);
  TEST_ASSERT_TRUE_MESSAGE(factory_raw->attempts >= 1,
                           "expected first attempt");
  TEST_ASSERT_TRUE_MESSAGE(f.AnyQuarantined(), "expected quarantine");
  TEST_ASSERT_EQUAL_UINT(0, f.connections->count_connections());
  TEST_ASSERT_EQUAL_UINT(0, f.cloud->servers().at(ServerId{7}).priority);
  auto attempts_after_first = factory_raw->attempts;

  f.ctx.PumpAt(t0, 128);
  TEST_ASSERT_EQUAL_INT(attempts_after_first, factory_raw->attempts);

  auto t_before =
      t0 + std::chrono::milliseconds{AE_CLOUD_SERVER_QUARANTINE_TIME_MS - 1};
  f.ctx.PumpAt(t_before, 8);
  TEST_ASSERT_EQUAL_INT(attempts_after_first, factory_raw->attempts);
}

void test_CloudImmediateUnusableCandidatesUseQuarantinePath() {
  auto factory = std::make_unique<CountingNullFactory>();
  auto* factory_raw = factory.get();
  CloudFixture f{std::move(factory), factory_raw, {ServerId{8}}, /*max*/ 2};
  TEST_ASSERT_TRUE(f.AnyQuarantined());

  std::vector<ServerId> quarantined_server_ids;
  auto quarantined_sub = f.connections->server_quarantined_event().Subscribe(
      [&](CloudServerConnection* server) {
        quarantined_server_ids.emplace_back(server->server_id());
      });

  // Construction performs the first reconciliation before the event
  // subscription. Retry both immediately unusable candidates after quarantine
  // release so their SubscribeToServerState failures are observable here.
  auto const retry_at =
      std::chrono::system_clock::now() +
      std::chrono::milliseconds{AE_CLOUD_SERVER_QUARANTINE_TIME_MS + 1};
  // Release the original quarantine tasks at the simulated expiry. Run the
  // resulting reconciliation at wall-clock time so quarantine tasks created
  // by the immediately unusable candidates are not already due.
  f.ctx.PumpAt(retry_at, 1);
  f.ctx.PumpAt(std::chrono::system_clock::now(), 1);

  TEST_ASSERT_TRUE_MESSAGE(quarantined_server_ids.size() >= 2,
                           "expected both candidates to be quarantined");
  TEST_ASSERT_TRUE(std::find(quarantined_server_ids.begin(),
                             quarantined_server_ids.end(),
                             ServerId{7}) != quarantined_server_ids.end());
  TEST_ASSERT_TRUE(std::find(quarantined_server_ids.begin(),
                             quarantined_server_ids.end(),
                             ServerId{8}) != quarantined_server_ids.end());
  TEST_ASSERT_EQUAL_UINT(0, f.connections->count_connections());
  for (auto* server : f.connections->servers()) {
    TEST_ASSERT_TRUE(server->quarantine());
    TEST_ASSERT_FALSE(CloudServerConnectionsTestAccess::HasStateSubscription(
        *f.connections, *server));
    TEST_ASSERT_FALSE(CloudServerConnectionsTestAccess::HasErrorSubscription(
        *f.connections, *server));
    TEST_ASSERT_TRUE(CloudServerConnectionsTestAccess::HasQuarantineTask(
        *f.connections, *server));
  }
}

void test_CloudServerPointersRemainStableAcrossQuarantine() {
  auto factory = std::make_unique<CountingNullFactory>();
  auto* factory_raw = factory.get();
  CloudFixture f{
      std::move(factory), factory_raw, {ServerId{8}, ServerId{9}}, /*max*/ 0};
  auto const pointers = f.connections->servers();

  TEST_ASSERT_TRUE(CloudServerConnectionsTestAccess::Quarantine(*f.connections,
                                                                *pointers[0]));
  CloudServerConnectionsTestAccess::Release(*f.connections, *pointers[0]);
  TEST_ASSERT_FALSE(CloudServerConnectionsTestAccess::HasQuarantineTask(
      *f.connections, *pointers[0]));

  for (auto* pointer : pointers) {
    auto const current = std::find(f.connections->servers().begin(),
                                   f.connections->servers().end(), pointer);
    TEST_ASSERT_TRUE(current != f.connections->servers().end());
  }
}

void test_CloudQuarantineReleaseAfterExpiry() {
  auto factory = std::make_unique<CountingNullFactory>();
  auto* factory_raw = factory.get();
  CloudFixture f{std::move(factory), factory_raw};

  int releases = 0;
  auto r_sub = f.connections->server_quarantine_release_event().Subscribe(
      [&](CloudServerConnection*) { ++releases; });

  auto t0 = std::chrono::system_clock::now();
  f.ctx.PumpAt(t0, 16);
  TEST_ASSERT_TRUE(f.AnyQuarantined());
  auto attempts_after_first = factory_raw->attempts;

  auto t_release =
      t0 + std::chrono::milliseconds{AE_CLOUD_SERVER_QUARANTINE_TIME_MS + 1};
  f.ctx.PumpAt(t_release, 4);
  TEST_ASSERT_TRUE_MESSAGE(releases >= 1, "expected quarantine release");
  TEST_ASSERT_TRUE_MESSAGE(factory_raw->attempts > attempts_after_first,
                           "expected reconnect attempt after release");
  TEST_ASSERT_TRUE_MESSAGE(factory_raw->attempts <= attempts_after_first + 4,
                           "too many attempts in one release wave");
}

void test_CloudQuarantineNoRecursiveLoopSameTimestamp() {
  auto factory = std::make_unique<CountingNullFactory>();
  auto* factory_raw = factory.get();
  CloudFixture f{std::move(factory), factory_raw};

  auto t0 = std::chrono::system_clock::now();
  f.ctx.PumpAt(t0, 4);
  auto attempts = factory_raw->attempts;
  f.ctx.PumpAt(t0, 64);
  TEST_ASSERT_EQUAL_INT(attempts, factory_raw->attempts);
}

void test_CloudQuarantineAttemptsBoundedOverSimulatedSecond() {
  auto factory = std::make_unique<CountingNullFactory>();
  auto* factory_raw = factory.get();
  CloudFixture f{std::move(factory), factory_raw};

  auto t0 = std::chrono::system_clock::now();
  f.ctx.PumpAt(t0, 8);
  auto attempts_after_first = factory_raw->attempts;
  TEST_ASSERT_TRUE(attempts_after_first >= 1);

  // Advance one quarantine period at a time using a fresh wall-clock base so
  // newly scheduled DelayedTasks are not immediately due under a far-future
  // Update timestamp (no wall-clock sleep).
  constexpr auto kWindowMs = 1000;
  auto const period = AE_CLOUD_SERVER_QUARANTINE_TIME_MS;
  auto const periods = (kWindowMs / period) + 1;
  for (int i = 0; i < periods; ++i) {
    auto now = std::chrono::system_clock::now();
    f.ctx.PumpAt(now + std::chrono::milliseconds{period + 1}, 2);
  }

  auto const max_attempts = attempts_after_first + periods * 2 + 2;
  TEST_ASSERT_TRUE_MESSAGE(
      factory_raw->attempts <= max_attempts,
      "too many attempts across simulated quarantine periods");
  TEST_ASSERT_TRUE_MESSAGE(
      factory_raw->attempts > attempts_after_first,
      "expected further attempts after quarantine periods");
}

void test_CloudFactoryBecomesAvailableAfterFailures() {
  auto factory = std::make_unique<SwitchableNullFactory>();
  auto* factory_raw = factory.get();
  CloudFixture f{std::move(factory), factory_raw};

  auto t0 = std::chrono::system_clock::now();
  f.ctx.PumpAt(t0, 8);
  TEST_ASSERT_TRUE(f.AnyQuarantined());
  auto attempts_before = factory_raw->attempts;

  factory_raw->available = true;
  auto t_release =
      t0 + std::chrono::milliseconds{AE_CLOUD_SERVER_QUARANTINE_TIME_MS + 1};
  f.ctx.PumpAt(t_release, 4);
  TEST_ASSERT_TRUE_MESSAGE(factory_raw->attempts > attempts_before,
                           "release must attempt reconnect after available");
}

}  // namespace test_cloud_quarantine_loop
}  // namespace ae

int run_test_cloud_quarantine_loop() {
  using namespace ae::test_cloud_quarantine_loop;  // NOLINT

  UNITY_BEGIN();
  RUN_TEST(test_CloudQuarantineAndReleasePreserveCanonicalOrder);
  RUN_TEST(test_CloudQuarantineDoesNotBusyLoop);
  RUN_TEST(test_CloudImmediateUnusableCandidatesUseQuarantinePath);
  RUN_TEST(test_CloudServerPointersRemainStableAcrossQuarantine);
  RUN_TEST(test_CloudQuarantineReleaseAfterExpiry);
  RUN_TEST(test_CloudQuarantineNoRecursiveLoopSameTimestamp);
  RUN_TEST(test_CloudQuarantineAttemptsBoundedOverSimulatedSecond);
  RUN_TEST(test_CloudFactoryBecomesAvailableAfterFailures);
  return UNITY_END();
}
