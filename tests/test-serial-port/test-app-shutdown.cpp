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

#include <unity.h>
#include <chrono>
#include <memory>
#include "aether/config.h"
#if AE_DISTILLATION
#  include "aether-objects/domain_storage/ram_domain_storage.h"
#  include "aether/aether_app.h"

namespace ae::test_app_shutdown {
class ShutdownTestAdapter final : public Adapter {
  AE_OBJECT(ShutdownTestAdapter, Adapter, 0)
 protected:
  ShutdownTestAdapter() = default;

 public:
  ShutdownTestAdapter(ObjProp prop, TaskScheduler& scheduler, int& finished)
      : Adapter{prop}, scheduler_{&scheduler}, finished_{&finished} {}
  AE_OBJECT_REFLECT()
  std::vector<AccessPoint::ptr> access_points() override { return {}; }
  IAdapterStop& Stop() override {
    ++stop_count;
    task_ = scheduler_->DelayedTask(
        [this]() {
          ++*finished_;
          action_.Finish();
        },
        Now() + std::chrono::milliseconds{20});
    return action_;
  }
  int stop_count{};

 private:
  TaskScheduler* scheduler_{};
  int* finished_{};
  IAdapterStop action_;
  TaskSubscription task_;
};

std::unique_ptr<AetherApp> MakeApp(int& finished,
                                   ShutdownTestAdapter::ptr& adapter) {
  return AetherApp::Construct(
      AetherAppContext{[]() {
        return std::make_unique<RamDomainStorage>();
      }}.AddAdapterFactory([&](AetherAppContext const& context)
                               -> Adapter::ptr {
        adapter = ShutdownTestAdapter::ptr::Create(
            context.domain(), *context.aether()->task_scheduler, finished);
        return adapter;
      }));
}

void test_ExitWaitsForAdapterAndKeepsOriginalExitCode() {
  int finished = 0;
  ShutdownTestAdapter::ptr adapter;
  auto app = MakeApp(finished, adapter);
  app->Exit(7);
  app->Exit(9);
  TEST_ASSERT_FALSE(app->IsExited());
  app->Update(Now());
  TEST_ASSERT_EQUAL_INT(1, adapter->stop_count);
  TEST_ASSERT_EQUAL_INT(0, finished);
  TEST_ASSERT_FALSE(app->IsExited());
  for (int i = 0; i < 8; ++i) {
    app->Update(Now() + std::chrono::milliseconds{100});
  }
  TEST_ASSERT_EQUAL_INT(1, finished);
  TEST_ASSERT_TRUE(app->IsExited());
  TEST_ASSERT_EQUAL_INT(7, app->ExitCode());
  // Release the external persistent reference before its domain is destroyed.
  adapter.Reset();
}

void test_DestructorWaitsForAdapterShutdown() {
  int finished = 0;
  ShutdownTestAdapter::ptr adapter;
  auto app = MakeApp(finished, adapter);
  adapter.Reset();
  app.reset();
  TEST_ASSERT_EQUAL_INT(1, finished);
}

void test_AdapterStopCompletesImmediatelyWithoutPendingWork() {
  AdapterStop empty;
  TEST_ASSERT_TRUE(empty.is_finished());
  Action completed;
  completed.Finish();
  AdapterStop stop{completed};
  TEST_ASSERT_TRUE(stop.is_finished());
}

void test_AdapterStopWaitsForDriverAndCompletesOnlyOnce() {
  Action driver;
  AdapterStop stop{driver};
  int completions = 0;
  Subscription sub = stop.finished_event().Subscribe([&]() { ++completions; });
  TEST_ASSERT_FALSE(stop.is_finished());
  driver.Finish();
  TEST_ASSERT_TRUE(stop.is_finished());
  TEST_ASSERT_EQUAL_INT(1, completions);
  driver.Finish();
  TEST_ASSERT_EQUAL_INT(1, completions);
}

void test_AdapterStopUnsubscribesWhenDestroyed() {
  Action driver;
  {
    AdapterStop stop{driver};
    TEST_ASSERT_FALSE(stop.is_finished());
  }
  driver.Finish();
  TEST_ASSERT_TRUE(driver.is_finished());
}
}  // namespace ae::test_app_shutdown
#endif
int test_app_shutdown() {
#if AE_DISTILLATION
  using namespace ae::test_app_shutdown;  // NOLINT: Unity suite entry.
  UNITY_BEGIN();
  RUN_TEST(test_ExitWaitsForAdapterAndKeepsOriginalExitCode);
  RUN_TEST(test_DestructorWaitsForAdapterShutdown);
  RUN_TEST(test_AdapterStopCompletesImmediatelyWithoutPendingWork);
  RUN_TEST(test_AdapterStopWaitsForDriverAndCompletesOnlyOnce);
  RUN_TEST(test_AdapterStopUnsubscribesWhenDestroyed);
  return UNITY_END();
#else
  return 0;
#endif
}
