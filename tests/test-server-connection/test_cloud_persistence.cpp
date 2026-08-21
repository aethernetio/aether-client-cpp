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

#include <memory>
#include <utility>
#include <vector>

#include <unity.h>

#include "aether/adapter_registry.h"
#include "aether/ae_context.h"
#include "aether/cloud.h"
#include "aether/cloud_connections/cloud_server_connections.h"
#include "aether/obj/domain.h"
#include "aether/server.h"
#include "aether/server_connections/client_server_connection.h"
#include "aether/server_connections/iserver_connection_factory.h"
#include "aether/types/address.h"
#include "aether/types/server_id.h"
#include "aether/work_cloud.h"

#include "tests/test-object-system/map_domain_storage.h"

namespace ae {
namespace test_cloud_persistence {
struct TestContext {
  AeCtx ToAeContext() const {
    static constexpr auto table =
        AeCtxTable{nullptr, [](void* obj) -> TaskScheduler& {
                     return static_cast<TestContext*>(obj)->sched;
                   }};
    return AeCtx{const_cast<TestContext*>(this), &table};  // NOLINT
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

void test_CloudSetServersReplacesEntries() {
  MapDomainStorage storage;
  Domain domain{Now(), storage};
  auto registry = AdapterRegistry::ptr::Create(CreateWith{domain});
  auto first = Server::ptr::Create(CreateWith{domain}, ServerId{7},
                                   std::vector<Endpoint>{}, registry);
  auto second = Server::ptr::Create(CreateWith{domain}, ServerId{8},
                                    std::vector<Endpoint>{}, registry);
  auto third = Server::ptr::Create(CreateWith{domain}, ServerId{9},
                                   std::vector<Endpoint>{}, registry);
  auto fourth = Server::ptr::Create(CreateWith{domain}, ServerId{10},
                                    std::vector<Endpoint>{}, registry);
  auto cloud = Cloud::ptr::Create(CreateWith{domain});

  cloud->SetServers({first, second});
  cloud->SetServers({third, fourth});

  TEST_ASSERT_EQUAL_UINT(2, cloud->servers().size());
  TEST_ASSERT_FALSE(cloud->servers().contains(ServerId{7}));
  TEST_ASSERT_FALSE(cloud->servers().contains(ServerId{8}));
  TEST_ASSERT_EQUAL_UINT(0, cloud->servers().at(ServerId{9}).priority);
  TEST_ASSERT_EQUAL_UINT(1, cloud->servers().at(ServerId{10}).priority);
}

void test_CloudAddServerAppendsAndPreservesExistingPriority() {
  auto factory = std::make_unique<CountingNullFactory>();
  auto* factory_raw = factory.get();
  CloudFixture f{std::move(factory), factory_raw};
  f.connections.reset();
  auto second_server = Server::ptr::Create(CreateWith{f.domain}, ServerId{8},
                                           std::vector<Endpoint>{}, f.registry);
  auto replacement = Server::ptr::Create(CreateWith{f.domain}, ServerId{7},
                                         std::vector<Endpoint>{}, f.registry);

  f.cloud->servers().at(ServerId{7}).priority = 12;
  f.cloud->AddServer(second_server);
  f.cloud->AddServer(replacement);

  TEST_ASSERT_EQUAL_UINT(13, f.cloud->servers().at(ServerId{8}).priority);
  TEST_ASSERT_EQUAL_UINT(12, f.cloud->servers().at(ServerId{7}).priority);
  TEST_ASSERT_EQUAL_UINT(replacement.id().id(),
                         f.cloud->servers().at(ServerId{7}).server.id().id());
}

void test_CloudServerConnectionPriorityUsesCloudMap() {
  auto factory = std::make_unique<CountingNullFactory>();
  auto* factory_raw = factory.get();
  CloudFixture f{std::move(factory), factory_raw};

  auto* server_connection = f.connections->servers().front();
  TEST_ASSERT_EQUAL_UINT(0, server_connection->priority());
  server_connection->SetPriority(12);
  TEST_ASSERT_EQUAL_UINT(12, server_connection->priority());
  TEST_ASSERT_EQUAL_UINT(12, f.cloud->servers().at(ServerId{7}).priority);
}

void test_CloudEqualPrioritiesDoNotRequireTieOrder() {
  auto factory = std::make_unique<CountingNullFactory>();
  auto* factory_raw = factory.get();
  CloudFixture f{std::move(factory), factory_raw};
  f.connections.reset();
  auto second = Server::ptr::Create(CreateWith{f.domain}, ServerId{8},
                                    std::vector<Endpoint>{}, f.registry);
  f.cloud->AddServer(second);
  f.cloud->servers().at(ServerId{7}).priority = 0;
  f.cloud->servers().at(ServerId{8}).priority = 0;

  auto reordered_factory = std::make_unique<CountingNullFactory>();
  auto* reordered_factory_raw = reordered_factory.get();
  CloudServerConnections connections{f.ae_ctx, f.cloud.Load(),
                                     std::move(reordered_factory), /*max*/ 0};

  auto const& servers = connections.servers();
  TEST_ASSERT_EQUAL_UINT(2, servers.size());
  TEST_ASSERT_TRUE((servers[0]->server_id() == ServerId{7} &&
                    servers[1]->server_id() == ServerId{8}) ||
                   (servers[0]->server_id() == ServerId{8} &&
                    servers[1]->server_id() == ServerId{7}));
  TEST_ASSERT_EQUAL_UINT(0, servers[0]->priority());
  TEST_ASSERT_EQUAL_UINT(1, servers[1]->priority());
  TEST_ASSERT_EQUAL_INT(0, reordered_factory_raw->attempts);
}

void test_CloudServerConnectionServerReferencesCloudMapEntry() {
  auto factory = std::make_unique<CountingNullFactory>();
  auto* factory_raw = factory.get();
  CloudFixture f{std::move(factory), factory_raw};

  auto* server_connection = f.connections->servers().front();
  auto const& server = server_connection->server();

  TEST_ASSERT_EQUAL_PTR(&f.cloud->servers().at(ServerId{7}).server, &server);
  TEST_ASSERT_TRUE(server.is_loaded());
  TEST_ASSERT_EQUAL_UINT(7, server->server_id);
}

void test_CloudServerConnectionPriorityRoundTripsAndRestoresSelectionOrder() {
  MapDomainStorage storage;
  Domain domain{Now(), storage};
  auto registry = AdapterRegistry::ptr::Create(CreateWith{domain});
  auto first = Server::ptr::Create(CreateWith{domain}, ServerId{10},
                                   std::vector<Endpoint>{}, registry);
  auto second = Server::ptr::Create(CreateWith{domain}, ServerId{20},
                                    std::vector<Endpoint>{}, registry);
  auto third = Server::ptr::Create(CreateWith{domain}, ServerId{30},
                                   std::vector<Endpoint>{}, registry);
  auto cloud = WorkCloud::ptr::Create(CreateWith{domain}.with_id(100), Uid{});
  cloud->AddServer(first);
  cloud->AddServer(second);
  cloud->AddServer(third);
  int initial_attempts{};
  std::size_t initial_overflows{};
  {
    TestContext initial_context;
    AeContext initial_ae_context{initial_context};
    auto initial_factory = std::make_unique<CountingNullFactory>();
    auto* initial_factory_raw = initial_factory.get();
    CloudServerConnections initial_connections{initial_ae_context, cloud.Load(),
                                               std::move(initial_factory),
                                               /*max*/ 0};

    initial_connections.servers().at(0)->SetPriority(2);
    initial_connections.servers().at(1)->SetPriority(1);
    initial_connections.servers().at(2)->SetPriority(0);
    initial_attempts = initial_factory_raw->attempts;
    initial_overflows = initial_context.sched.overflow_counter();
    cloud.Save();
  }
  TEST_ASSERT_EQUAL_INT(0, initial_attempts);
  TEST_ASSERT_EQUAL_UINT(0, initial_overflows);

  Domain restarted_domain{Now(), storage};
  auto restored_cloud =
      WorkCloud::ptr::Declare(CreateWith{restarted_domain}.with_id(cloud.id()));
  restored_cloud.Load();

  TEST_ASSERT_EQUAL_UINT(3, restored_cloud->servers().size());
  TEST_ASSERT_EQUAL_UINT(2,
                         restored_cloud->servers().at(ServerId{10}).priority);
  TEST_ASSERT_EQUAL_UINT(1,
                         restored_cloud->servers().at(ServerId{20}).priority);
  TEST_ASSERT_EQUAL_UINT(0,
                         restored_cloud->servers().at(ServerId{30}).priority);

  struct RestoredConnectionState {
    int attempts{};
    std::size_t overflows{};
    std::vector<ServerId> server_ids;
  } restored_connection_state;
  {
    TestContext restored_context;
    AeContext restored_ae_context{restored_context};
    auto factory = std::make_unique<CountingNullFactory>();
    auto* factory_raw = factory.get();
    CloudServerConnections connections{restored_ae_context,
                                       restored_cloud.Load(),
                                       std::move(factory), /*max*/ 0};

    auto const& servers = connections.servers();
    restored_connection_state.attempts = factory_raw->attempts;
    restored_connection_state.overflows =
        restored_context.sched.overflow_counter();
    for (std::size_t i = 0; i < servers.size(); ++i) {
      restored_connection_state.server_ids.emplace_back(
          servers[i]->server_id());
    }
  }
  TEST_ASSERT_EQUAL_INT(0, restored_connection_state.attempts);
  TEST_ASSERT_EQUAL_UINT(0, restored_connection_state.overflows);
  TEST_ASSERT_EQUAL_UINT(2,
                         restored_cloud->servers().at(ServerId{10}).priority);
  TEST_ASSERT_EQUAL_UINT(1,
                         restored_cloud->servers().at(ServerId{20}).priority);
  TEST_ASSERT_EQUAL_UINT(0,
                         restored_cloud->servers().at(ServerId{30}).priority);
  TEST_ASSERT_EQUAL_UINT(ServerId{30},
                         restored_connection_state.server_ids.at(0));
  TEST_ASSERT_EQUAL_UINT(ServerId{20},
                         restored_connection_state.server_ids.at(1));
  TEST_ASSERT_EQUAL_UINT(ServerId{10},
                         restored_connection_state.server_ids.at(2));
  for (std::size_t i = 0; i < restored_connection_state.server_ids.size();
       ++i) {
    TEST_ASSERT_EQUAL_UINT(static_cast<unsigned int>(i),
                           static_cast<unsigned int>(
                               restored_cloud->servers()
                                   .at(restored_connection_state.server_ids[i])
                                   .priority));
  }
}

}  // namespace test_cloud_persistence
}  // namespace ae

int run_test_cloud_persistence() {
  using namespace ae::test_cloud_persistence;  // NOLINT

  UNITY_BEGIN();
  RUN_TEST(test_CloudSetServersReplacesEntries);
  RUN_TEST(test_CloudAddServerAppendsAndPreservesExistingPriority);
  RUN_TEST(test_CloudServerConnectionPriorityUsesCloudMap);
  RUN_TEST(test_CloudEqualPrioritiesDoNotRequireTieOrder);
  RUN_TEST(test_CloudServerConnectionServerReferencesCloudMapEntry);
  RUN_TEST(
      test_CloudServerConnectionPriorityRoundTripsAndRestoresSelectionOrder);
  return UNITY_END();
}
