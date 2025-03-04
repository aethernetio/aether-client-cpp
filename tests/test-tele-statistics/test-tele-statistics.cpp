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

#if defined AE_DISTILLATION && (AE_TELE_ENABLED == 1) && \
    (AE_TELE_LOG_CONSOLE == 1)
#  include <iostream>

#  include "aether/common.h"
#  include "aether/tele/tele.h"
#  include "aether/tele/sink.h"
#  include "aether/tele/traps/statistics_trap.h"
#  include "aether/tele/traps/io_stream_traps.h"
#  include "aether/tele/configs/sink_to_statistics_trap.h"

#  include "aether/tele/traps/tele_statistics.h"
#  include "aether/port/file_systems/file_system_std.h"

static ae::Ptr<ae::tele::statistics::StatisticsTrap> statistics_trap;

void InitTeleSink(ae::Ptr<ae::tele::statistics::StatisticsTrap> const& st) {
  auto trap = ae::MakePtr<ae::tele::StatisticsObjectAndStreamTrap>(
      st, ae::MakePtr<ae::tele::IoStreamTrap>(std::cout));

  TELE_SINK::InitSink(std::move(trap));

  AE_TELED_DEBUG("Tele sink initialized");
}

void setUp() {
  statistics_trap = ae::MakePtr<ae::tele::statistics::StatisticsTrap>();
  InitTeleSink(statistics_trap);
  // start with clean state
  auto fs = ae::FileSystemStdFacility{};
  fs.remove_all();
}
void tearDown() {}

namespace ae::tele::test {

void test_StatisticsRotation() {
  auto fs = ae::FileSystemStdFacility{};
  fs.remove_all();
  auto domain = ae::Domain{ae::ClockType::now(), fs};

  TeleStatistics::ptr tele_statistics = domain.CreateObj<TeleStatistics>(1);
  tele_statistics->trap()->MergeStatistics(*statistics_trap);
  // set 100 byte
  tele_statistics->trap()->statistics_store_.SetSizeLimit(100);

  InitTeleSink(tele_statistics->trap());
  AE_TELED_DEBUG("12");
  auto statistics_size =
      tele_statistics->trap()->statistics_store_.Get()->Size();
  TEST_ASSERT_LESS_THAN(100, statistics_size);
  tele_statistics->trap()->statistics_store_.SetSizeLimit(1);
  auto zero_size = tele_statistics->trap()->statistics_store_.Get()->Size();
  TEST_ASSERT_EQUAL(0, zero_size);
}

void test_SaveLoadTeleStatistics() {
  auto fs = ae::FileSystemStdFacility{};
  fs.remove_all();
  auto domain = ae::Domain{ae::ClockType::now(), fs};

  AE_TELE_ENV();

  TeleStatistics::ptr tele_statistics = domain.CreateObj<TeleStatistics>(1);
  tele_statistics->trap()->MergeStatistics(*statistics_trap);
  InitTeleSink(tele_statistics->trap());
  AE_TELED_DEBUG("12");
  auto statistics_size =
      tele_statistics->trap()->statistics_store_.Get()->Size();

  // use new trap to prevent statistics change while save
  auto temp_trap = MakePtr<ae::tele::statistics::StatisticsTrap>();
  InitTeleSink(temp_trap);

  domain.SaveRoot(tele_statistics);

  // load stored object in new instance
  auto domain2 = ae::Domain{ae::ClockType::now(), fs};
  TeleStatistics::ptr tele_statistics2;
  tele_statistics2.SetId(1);
  domain2.LoadRoot(tele_statistics2);
  TEST_ASSERT(static_cast<bool>(tele_statistics2));

  auto statistics_size2 =
      tele_statistics2->trap()->statistics_store_.Get()->Size();
  TEST_ASSERT_EQUAL(statistics_size, statistics_size2);

  auto& logs1 = tele_statistics->trap()->statistics_store_.Get()->logs().logs;
  auto& logs2 = tele_statistics2->trap()->statistics_store_.Get()->logs().logs;
  auto& metrics1 =
      tele_statistics->trap()->statistics_store_.Get()->metrics().metrics;
  auto& metrics2 =
      tele_statistics2->trap()->statistics_store_.Get()->metrics().metrics;
  TEST_ASSERT_EQUAL(logs1.size(), logs2.size());
  TEST_ASSERT_EQUAL(metrics1.size(), metrics2.size());

  if constexpr (_AE_MODULE_CONFIG(MLog.value, AE_TELE_METRICS_MODULES) &&
                _AE_MODULE_CONFIG(MLog.value, AE_TELE_METRICS_DURATION)) {
    auto log_index = kLog.index;
    TEST_ASSERT_NOT_EQUAL(metrics1[log_index].invocations_count,
                          metrics2[log_index].invocations_count);
    TEST_ASSERT_NOT_EQUAL(metrics1[log_index].sum_duration,
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
