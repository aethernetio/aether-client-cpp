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

#include "tests/test-object-system/map_domain_storage.h"

namespace ae {
namespace {

static_assert(AE_CLOUD_SERVER_QUARANTINE_TIME_MS == 500 ||
                  AE_CLOUD_SERVER_QUARANTINE_TIME_MS == 250,
              "quarantine must be the bounded 500/250 ms recovery window");

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
  RcPtr<ClientServerConnection> CreateConnection(
      Ptr<Server> const& /*server*/) override {
    ++attempts;
    return {};
  }
  int attempts{0};
};

class SwitchableNullFactory final : public IServerConnectionFactory {
 public:
  RcPtr<ClientServerConnection> CreateConnection(
      Ptr<Server> const& /*server*/) override {
    ++attempts;
    return {};
  }
  bool available{false};
  int attempts{0};
};

struct CloudFixture {
  CloudFixture(std::unique_ptr<IServerConnectionFactory> factory,
               IServerConnectionFactory* raw)
      : ae_ctx{ctx},
        domain{Now(), storage},
        registry{AdapterRegistry::ptr::Create(CreateWith{domain})},
        server{Server::ptr::Create(CreateWith{domain}, ServerId{7},
                                   std::vector<Endpoint>{}, registry)},
        cloud{Cloud::ptr::Create(CreateWith{domain})},
        factory_raw{raw} {
    cloud->AddServer(server);
    connections = std::make_unique<CloudServerConnections>(
        ae_ctx, cloud.Load(), std::move(factory), /*max*/ 1);
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

void test_cloud_quarantine_does_not_busy_loop() {
  auto factory = std::make_unique<CountingNullFactory>();
  auto* factory_raw = factory.get();
  CloudFixture f{std::move(factory), factory_raw};

  auto t0 = std::chrono::system_clock::now();
  f.ctx.PumpAt(t0, 16);
  TEST_ASSERT_TRUE_MESSAGE(factory_raw->attempts >= 1,
                           "expected first attempt");
  TEST_ASSERT_TRUE_MESSAGE(f.AnyQuarantined(), "expected quarantine");
  TEST_ASSERT_EQUAL_UINT(0, f.connections->count_connections());
  auto attempts_after_first = factory_raw->attempts;

  f.ctx.PumpAt(t0, 128);
  TEST_ASSERT_EQUAL_INT(attempts_after_first, factory_raw->attempts);

  auto t_before =
      t0 + std::chrono::milliseconds{AE_CLOUD_SERVER_QUARANTINE_TIME_MS - 1};
  f.ctx.PumpAt(t_before, 8);
  TEST_ASSERT_EQUAL_INT(attempts_after_first, factory_raw->attempts);
}

void test_cloud_quarantine_release_after_expiry() {
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
  TEST_ASSERT_TRUE_MESSAGE(
      factory_raw->attempts <= attempts_after_first + 4,
      "too many attempts in one release wave");
}

void test_cloud_quarantine_no_recursive_loop_same_timestamp() {
  auto factory = std::make_unique<CountingNullFactory>();
  auto* factory_raw = factory.get();
  CloudFixture f{std::move(factory), factory_raw};

  auto t0 = std::chrono::system_clock::now();
  f.ctx.PumpAt(t0, 4);
  auto attempts = factory_raw->attempts;
  f.ctx.PumpAt(t0, 64);
  TEST_ASSERT_EQUAL_INT(attempts, factory_raw->attempts);
}

void test_cloud_quarantine_attempts_bounded_over_simulated_second() {
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

void test_cloud_factory_becomes_available_after_failures() {
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

}  // namespace
}  // namespace ae

int run_test_cloud_quarantine_loop() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_cloud_quarantine_does_not_busy_loop);
  RUN_TEST(ae::test_cloud_quarantine_release_after_expiry);
  RUN_TEST(ae::test_cloud_quarantine_no_recursive_loop_same_timestamp);
  RUN_TEST(ae::test_cloud_quarantine_attempts_bounded_over_simulated_second);
  RUN_TEST(ae::test_cloud_factory_becomes_available_after_failures);
  return UNITY_END();
}
