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

#include <cstdint>
#include <vector>

#include <unity.h>

#include "aether/config.h"

#if AE_SUPPORT_REGISTRATION

#  include "aether-objects/domain_storage/ram_domain_storage.h"
#  include "aether-objects/obj/domain.h"

#  include "aether/adapter_registry.h"
#  include "aether/ae_context.h"
#  include "aether/aether.h"
#  include "aether/registration/root_server_select_stream.h"
#  include "aether/registration_cloud.h"
#  include "aether/server.h"
#  include "aether/types/address.h"
#  include "aether/types/server_id.h"

namespace ae {

struct RootServerSelectStreamTestAccess {
  static std::uint16_t ServerPriority(RootServerSelectStream const& stream) {
    return stream.server_priority_;
  }

  static void ServerError(RootServerSelectStream& stream) {
    stream.ServerError();
  }
};

namespace test_registration_root_server_select {

struct TestContext {
  AeCtx ToAeContext() const {
    static constexpr auto table =
        AeCtxTable{nullptr, [](void* obj) -> TaskScheduler& {
                     return static_cast<TestContext*>(obj)->scheduler;
                   }};
    return AeCtx{const_cast<TestContext*>(this), &table};  // NOLINT
  }

  TaskScheduler scheduler;
};

struct Fixture {
  Fixture()
      : ae_context{context},
        domain{storage},
        aether{Aether::ptr::Create(CreateWith{domain})},
        registry{AdapterRegistry::ptr::Create(CreateWith{domain})},
        cloud{RegistrationCloud::ptr::Create(CreateWith{domain}, aether)} {}

  Server::ptr AddServer(ServerId id, std::uint16_t priority) {
    auto server = Server::ptr::Create(CreateWith{domain}, id,
                                      std::vector<Endpoint>{}, registry);
    cloud->AddServer(server);
    cloud->servers().at(id).priority = priority;
    return server;
  }

  TestContext context;
  AeContext ae_context;
  RamDomainStorage storage;
  Domain domain;
  Aether::ptr aether;
  AdapterRegistry::ptr registry;
  RegistrationCloud::ptr cloud;
};

void test_SingleRegistrationServerSelectsPriorityZero() {
  Fixture f;
  f.AddServer(ServerId{0}, 0);

  RootServerSelectStream stream{f.ae_context, f.cloud.Load()};
  int cloud_errors = 0;
  stream.cloud_error_event().Subscribe([&]() { ++cloud_errors; });

  TEST_ASSERT_EQUAL_UINT(
      1, RootServerSelectStreamTestAccess::ServerPriority(stream));
  RootServerSelectStreamTestAccess::ServerError(stream);
  TEST_ASSERT_EQUAL_INT(1, cloud_errors);
}

void test_RegistrationServersFailOverByNextPriority() {
  Fixture f;
  f.AddServer(ServerId{80}, 1);
  f.AddServer(ServerId{2}, 0);

  RootServerSelectStream stream{f.ae_context, f.cloud.Load()};
  int cloud_errors = 0;
  stream.cloud_error_event().Subscribe([&]() { ++cloud_errors; });

  TEST_ASSERT_EQUAL_UINT(
      1, RootServerSelectStreamTestAccess::ServerPriority(stream));
  RootServerSelectStreamTestAccess::ServerError(stream);
  TEST_ASSERT_EQUAL_UINT(
      2, RootServerSelectStreamTestAccess::ServerPriority(stream));

  RootServerSelectStreamTestAccess::ServerError(stream);
  TEST_ASSERT_EQUAL_INT(1, cloud_errors);
}

}  // namespace test_registration_root_server_select
}  // namespace ae

int run_test_registration_root_server_select() {
  using namespace ae::test_registration_root_server_select;  // NOLINT

  UNITY_BEGIN();
  RUN_TEST(test_SingleRegistrationServerSelectsPriorityZero);
  RUN_TEST(test_RegistrationServersFailOverByNextPriority);
  return UNITY_END();
}

#endif  // AE_SUPPORT_REGISTRATION
