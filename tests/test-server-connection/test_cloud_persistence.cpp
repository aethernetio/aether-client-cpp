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

#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/domain.h"

#include "aether/adapter_registry.h"
#include "aether/adapters/ethernet.h"
#include "aether/ae_context.h"
#include "aether/aether.h"
#include "aether/channels/ethernet_channel.h"
#include "aether/cloud.h"
#include "aether/client.h"
#include "aether/connection_manager/client_cloud_manager.h"
#include "aether/connection_manager/get_cloud_aether.h"
#include "aether/cloud_connections/cloud_server_connections.h"
#include "aether/dns/dns_resolve.h"
#include "aether/poller/poller.h"
#include "aether/server.h"
#include "aether/server_connections/client_server_connection.h"
#include "aether/server_connections/iserver_connection_factory.h"
#include "aether/types/address.h"
#include "aether/types/server_id.h"
#include "aether/work_cloud.h"

namespace ae {
namespace test_cloud_persistence {
struct TestContext : public Env {
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

  TaskScheduler sched;
  EventSystem event_system;
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
        domain{storage, &ctx},
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
  RamDomainStorage storage;
  Domain domain;
  AdapterRegistry::ptr registry;
  Server::ptr server;
  Cloud::ptr cloud;
  IServerConnectionFactory* factory_raw{nullptr};
  std::unique_ptr<CloudServerConnections> connections;
};

void test_CloudSetServersReplacesEntries() {
  TestContext ctx;
  RamDomainStorage storage;
  Domain domain{storage, &ctx};
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
  TestContext ctx;
  RamDomainStorage storage;
  Domain domain{storage, &ctx};
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

  TestContext restored_context;
  Domain restarted_domain{storage, &restored_context};
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

void test_ServerRebuildsUnsupportedCachedChannelAndPreservesValidIds() {
  constexpr auto supported_protocol = AE_SUPPORT_TCP ? Protocol::kTcp : Protocol::kUdp;
  TestContext context;
  RamDomainStorage storage;
  ObjId server_id;
  ObjId valid_channel_id;
  {
    Domain domain{storage, &context};
    auto registry = AdapterRegistry::ptr::Create(CreateWith{domain});
    registry->Add(EthernetAdapter::ptr::Create(
        CreateWith{domain}, Aether::ptr{}, IPoller::ptr{}, DnsResolver::ptr{}));
    Endpoint endpoint{{IpV4Addr{127, 0, 0, 1}, 9020}, supported_protocol};
    auto server = Server::ptr::Create(CreateWith{domain}, ServerId{7},
                                      std::vector{endpoint}, registry);
    TEST_ASSERT_EQUAL_UINT(1, server->channels.size());
    server_id = server.id();
    valid_channel_id = server->channels.front().id();
    server.Save();
  }
  {
    Domain domain{storage, &context};
    auto server = Server::ptr::Declare(CreateWith{domain}.with_id(server_id));
    server.Load();
    TEST_ASSERT_TRUE(valid_channel_id == server->channels.front().id());
    auto channel = static_cast<EthernetChannel*>(&*server->channels.front());
    channel->address.protocol = static_cast<Protocol>(255);
    server.Save();
  }
  {
    Domain domain{storage, &context};
    auto server = Server::ptr::Declare(CreateWith{domain}.with_id(server_id));
    server.Load();
    TEST_ASSERT_EQUAL_UINT(1, server->channels.size());
    TEST_ASSERT_TRUE(server->channels.front().id() != valid_channel_id);
    TEST_ASSERT_TRUE(server->channels.front()->endpoint()->protocol ==
                     supported_protocol);
  }
}

void test_CloudResolutionCompletesAndReleasesActionPool() {
  TestContext context;
  AeContext ae_context{context};
  RamDomainStorage storage;
  Domain domain{storage, &context};
  auto aether = Aether::ptr::Create(CreateWith{domain});
  auto client = Client::ptr::Create(CreateWith{domain}, aether);
  auto manager =
      ClientCloudManager::ptr::Create(CreateWith{domain}, aether, client);
  auto cloud = Cloud::ptr::Create(CreateWith{domain});
  // Inject decoded server publications without a real network connection.
  auto& updates = const_cast<ClientCloudManager::CloudUpdateEvent&>(
      manager->cloud_update_event());
  CloudServerConnections connections{
      ae_context, cloud.Load(), std::make_unique<CountingNullFactory>(), 1};
  ActionPool<AeContext, GetCloudFromAether, 2> actions{ae_context};
  auto const uid = Uid::FromString("11111111-1111-1111-1111-111111111111");
  auto const other = Uid::FromString("22222222-2222-2222-2222-222222222222");
  // More resolutions than pool slots, covering both terminal result kinds.
  for (int iteration = 0; iteration < 12; ++iteration) {
    auto* action = actions.Create(ae_context, *manager, connections, uid);
    TEST_ASSERT_NOT_NULL(action);
    int results = 0;
    bool success = false;
    Subscription result_sub =
        action->result_event().Subscribe([&](auto const& result) {
          ++results;
          success = result.IsOk();
        });
    updates.Emit(other, Ok<Cloud::ptr const&>{cloud});
    TEST_ASSERT_EQUAL_INT(0, results);
    TEST_ASSERT_FALSE(action->is_finished());
    bool const expected_success = iteration % 2 == 0;
    if (expected_success) {
      updates.Emit(uid, Ok<Cloud::ptr const&>{cloud});
    } else {
      updates.Emit(uid, Error{-1});
    }
    TEST_ASSERT_EQUAL_INT(1, results);
    TEST_ASSERT_EQUAL(expected_success, success);
    TEST_ASSERT_TRUE(action->is_finished());
    // A second cloud publication before deferred reclamation is not a second
    // terminal result and must not queue destruction of the same slot twice.
    updates.Emit(uid, Ok<Cloud::ptr const&>{cloud});
    TEST_ASSERT_EQUAL_INT(1, results);
    result_sub.Reset();
    context.Update();
    context.Update();
    TEST_ASSERT_TRUE(actions.empty());
  }
}

}  // namespace test_cloud_persistence
}  // namespace ae

int run_test_cloud_persistence() {
  using namespace ae::test_cloud_persistence;  // NOLINT

  UNITY_BEGIN();
  RUN_TEST(test_CloudResolutionCompletesAndReleasesActionPool);
  RUN_TEST(test_ServerRebuildsUnsupportedCachedChannelAndPreservesValidIds);
  RUN_TEST(test_CloudSetServersReplacesEntries);
  RUN_TEST(test_CloudAddServerAppendsAndPreservesExistingPriority);
  RUN_TEST(test_CloudServerConnectionPriorityUsesCloudMap);
  RUN_TEST(test_CloudEqualPrioritiesDoNotRequireTieOrder);
  RUN_TEST(test_CloudServerConnectionServerReferencesCloudMapEntry);
  RUN_TEST(
      test_CloudServerConnectionPriorityRoundTripsAndRestoresSelectionOrder);
  return UNITY_END();
}
