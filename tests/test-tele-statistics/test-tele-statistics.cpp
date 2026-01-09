/*
 * Copyright 2024 Aethernet Inc.
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

#include "aether/config.h"

#if defined AE_DISTILLATION && (AE_TELE_ENABLED == 1)

#  include "aether/common.h"
#  include "aether/tele/tele.h"
#  include "aether/tele/sink.h"
#  include "aether/tele/traps/statistics_trap.h"

#  include "aether/tele/traps/tele_statistics.h"
#  include "aether/domain_storage/ram_domain_storage.h"

static std::shared_ptr<ae::tele::statistics::StatisticsTrap> statistics_trap;

void InitTeleSink(
    std::shared_ptr<ae::tele::statistics::StatisticsTrap> const& st) {
  TELE_SINK::Instance().SetTrap(st);

  AE_TELED_DEBUG("Tele sink initialized");
}

void setUp() {
  statistics_trap = std::make_shared<ae::tele::statistics::StatisticsTrap>();
  InitTeleSink(statistics_trap);
}

void tearDown() {}

namespace ae::tele::test {

void test_StatisticsRotation() {
  auto ram_ds = RamDomainStorage{};
  ram_ds.CleanUp();
  auto domain = ae::Domain{ram_ds};

  TeleStatistics tele_statistics = TeleStatistics{&domain};
  tele_statistics.trap()->MergeStatistics(*statistics_trap);
  // set 100 byte
  tele_statistics.trap()->statistics_store.SetSizeLimit(100);

  InitTeleSink(tele_statistics.trap());
  {
    AE_TELED_DEBUG("12");
  }
  auto statistics_size =
      std::get<statistics::RuntimeLog>(
          *tele_statistics.trap()->statistics_store.log_store())
          .size;
  TEST_ASSERT_LESS_THAN(100, statistics_size);
  tele_statistics.trap()->statistics_store.SetSizeLimit(1);
  // rotation happened
  auto zero_size = std::get<statistics::RuntimeLog>(
                       *tele_statistics.trap()->statistics_store.log_store())
                       .size;
  TEST_ASSERT_EQUAL(0, zero_size);
}

void test_SaveLoadTeleStatistics() {
  auto ram_ds = RamDomainStorage{};
  ram_ds.CleanUp();
  auto domain = ae::Domain{ram_ds};

  AE_TELE_ENV();

  TeleStatistics tele_statistics = TeleStatistics{&domain};
  tele_statistics.trap()->MergeStatistics(*statistics_trap);
  InitTeleSink(tele_statistics.trap());
  {
    AE_TELED_DEBUG("12");
  }
  auto statistics_size =
      std::get<statistics::RuntimeLog>(
          *tele_statistics.trap()->statistics_store.log_store())
          .size;

  // use new trap to prevent statistics change while save
  auto temp_trap = std::make_shared<ae::tele::statistics::StatisticsTrap>();
  InitTeleSink(temp_trap);

  tele_statistics.Save();

  // load stored object in new instance
  auto domain2 = ae::Domain{ram_ds};
  TeleStatistics tele_statistics2{&domain2};

  auto statistics_size2 =
      std::get<statistics::RuntimeLog>(
          *tele_statistics2.trap()->statistics_store.log_store())
          .size;
  TEST_ASSERT_EQUAL(statistics_size, statistics_size2);

  auto& metrics1 =
      tele_statistics.trap()->statistics_store.metrics_store().metrics;
  auto& metrics2 =
      tele_statistics2.trap()->statistics_store.metrics_store().metrics;
  TEST_ASSERT_EQUAL(metrics1.size(), metrics2.size());

  if constexpr (_AE_MODULE_CONFIG(MLog.id, AE_TELE_METRICS_MODULES) &&
                _AE_MODULE_CONFIG(MLog.id, AE_TELE_METRICS_DURATION)) {
    auto log_index = kLog.offset;
    TEST_ASSERT_EQUAL(metrics1[log_index].invocations_count,
                      metrics2[log_index].invocations_count);
    TEST_ASSERT_EQUAL(metrics1[log_index].sum_duration,
                      metrics2[log_index].sum_duration);
  }
}
}  // namespace ae::tele::test
#else
void setUp() {}
void tearDown() {}
#endif

int main() {
  UNITY_BEGIN();
#if defined AE_DISTILLATION && (AE_TELE_ENABLED == 1) && \
    (AE_TELE_LOG_CONSOLE == 1)
  RUN_TEST(ae::tele::test::test_StatisticsRotation);
  RUN_TEST(ae::tele::test::test_SaveLoadTeleStatistics);
#endif
  return UNITY_END();
}
