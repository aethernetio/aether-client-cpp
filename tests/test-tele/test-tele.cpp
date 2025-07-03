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

#include <chrono>
#include <list>
#include <map>
#include <thread>
#include <vector>
#include <iostream>

#include "aether/config.h"

// force to enable telemetry for this test
#if defined AE_TELE_ENABLED
#  undef AE_TELE_ENABLED
#  define AE_TELE_ENABLED 1
#endif

#if defined AE_TELE_METRICS_MODULES
#  undef AE_TELE_METRICS_MODULES
#  define AE_TELE_METRICS_MODULES AE_ALL
#endif

#if defined AE_TELE_METRICS_DURATION
#  undef AE_TELE_METRICS_DURATION
#  define AE_TELE_METRICS_DURATION AE_ALL
#endif

#define AETHER_TELE_TELE_H_

#include "aether/types/type_list.h"

#include "aether/tele/sink.h"
#include "aether/tele/tags.h"
#include "aether/tele/modules.h"
#include "aether/tele/defines.h"
#include "aether/tele/collectors.h"
#include "aether/tele/env_collectors.h"

#include "aether/tele/configs/config_provider.h"

#include "aether/tele/traps/proxy_trap.h"
#include "aether/tele/traps/io_stream_traps.h"
#include "aether/tele/traps/statistics_trap.h"

using SinkType = ae::tele::TeleSink<ae::tele::ConfigProvider>;

#define TELE_SINK SinkType
static std::shared_ptr<ae::tele::IoStreamTrap> trap;

void setUp() {
  trap = std::make_shared<ae::tele::IoStreamTrap>(std::cout);

  SinkType::Instance().SetTrap(trap);
}

void tearDown() {}

AE_TELE_MODULE(TestObj, 12, 1, 20);

AE_TAG(Zero, TestObj)
AE_TAG(One, TestObj)
AE_TAG(Two, TestObj)
AE_TAG(Three, TestObj)
// duplicated!
AE_TAG(Four, TestObj)
// duplicated!

AE_TAG(Test1, TestObj)
AE_TAG(Test2, TestObj)
AE_TAG(Test3, TestObj)

namespace ae::tele::test_tele {
void test_Register() {
  TEST_ASSERT_EQUAL(1, One.offset);
  TEST_ASSERT_EQUAL(2, Two.offset);
  TEST_ASSERT_EQUAL(3, Three.offset);
  TEST_ASSERT_EQUAL(4, Four.offset);
}

void test_SimpleTeleWithDuration() {
  {
    AE_TELE_DEBUG(Test1);
    AE_TELE_DEBUG(Test1, "format {}", 12);
    AE_TELE_INFO(Test2, "format {}", 24);
    AE_TELE_INFO(Test3, "format {}", 48);
    // must not compile
    // AE_TELE_INFO(Not_registered, "format {}", 96);
  }
  auto const& metrics = trap->metrics().at(Test1.offset + TestObj.index_start);

  TEST_ASSERT_EQUAL(2, metrics.invocations_count);
}

namespace tele_configuration {
struct TeleTrap final : public ITrap {
  struct MetricData {
    std::uint32_t count_{};
    std::uint32_t duration_{};
  };

  void AddInvoke(Tag const& tag, std::uint32_t count) override {
    metric_data_[tag.index()].count_ += count;
  }
  void AddInvokeDuration(Tag const& tag, Duration duration) override {
    metric_data_[tag.index()].duration_ +=
        static_cast<std::uint32_t>(duration.count());
  }
  void OpenLogLine(Tag const& tag) override {
    log_lines_.emplace_back();
    log_lines_.front().emplace_back(std::to_string(tag.index()));
  }
  void InvokeTime(TimePoint time) override {
    log_lines_.front().emplace_back(Format("{:%H:%M:%S}", time));
  }
  void WriteLevel(Level level) override {
    log_lines_.front().emplace_back(Format("{}", level));
  }
  void WriteModule(Module const& module) override {
    log_lines_.front().emplace_back(Format("{}", module));
  }
  void Location(std::string_view file, std::uint32_t line) override {
    auto pos = file.find_last_of("/\\");
    if (pos == std::string_view::npos) {
      file = "UNKNOWN FILE";
    }
    file = file.substr(pos + 1, file.size() - pos);
    log_lines_.front().emplace_back(Format("{file}:{line}", file, line));
  }
  void TagName(std::string_view name) override {
    log_lines_.front().emplace_back(name);
  }
  void Blob(std::uint8_t const* data, std::size_t size) override {
    log_lines_.front().emplace_back(
        std::string(reinterpret_cast<char const*>(data), size));
  }
  void CloseLogLine(Tag const& /*tag*/) override {}
  void WriteEnvData(EnvData const& /*env_data*/) override {}

  std::list<std::vector<std::string>> log_lines_;
  std::map<std::size_t, MetricData> metric_data_;
};

template <bool Count = true, bool Time = true, bool Index = true,
          bool StartTime = true, bool LevelModule = true, bool Location = true,
          bool Text = true, bool Blob = true>
struct ConfigProvider {
  template <bool... args>
  struct TeleConfig {
    static constexpr bool kCountMetrics = ArgAt<0>(args...);
    static constexpr bool kTimeMetrics = ArgAt<1>(args...);
    static constexpr bool kIndexLogs = ArgAt<2>(args...);
    static constexpr bool kStartTimeLogs = ArgAt<3>(args...);
    static constexpr bool kLevelModuleLogs = ArgAt<4>(args...);
    static constexpr bool kLocationLogs = ArgAt<5>(args...);
    static constexpr bool kNameLogs = ArgAt<6>(args...);
    static constexpr bool kBlobLogs = ArgAt<7>(args...);
  };

  template <bool... args>
  struct EnvConfig {
    static constexpr bool kCompiler = ArgAt<0>(args...);
    static constexpr bool kPlatformType = ArgAt<1>(args...);
    static constexpr bool kCompilationOptions = ArgAt<2>(args...);
    static constexpr bool kLibraryVersion = ArgAt<3>(args...);
    static constexpr bool kApiVersion = ArgAt<4>(args...);
    static constexpr bool kCpuType = ArgAt<5>(args...);
  };

  template <ae::tele::Level::underlined_t level, std::uint32_t module>
  using StaticTeleConfig = TeleConfig<Count, Time, Index, StartTime,
                                      LevelModule, Location, Text, Blob>;

  using StaticEnvConfig = EnvConfig<true, true, true, true, true>;
};
}  // namespace tele_configuration

void test_TeleConfigurations() {
  constexpr auto TestTag = Tag{11, TestObj, "Test"};
  {
    // all enabled
    using Sink = TeleSink<tele_configuration::ConfigProvider<>>;
    auto tele_trap = std::make_shared<tele_configuration::TeleTrap>();

    Sink::Instance().SetTrap(tele_trap);
    int remember_line = __LINE__ + 3;
    {
      auto t = Tele<Sink, Sink::TeleConfig<Level::kDebug, TestTag.module.id>>{
          Sink::Instance(), TestTag, Level{Level::kDebug}, __FILE__, __LINE__,
          "message {}",     12};

      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    TEST_ASSERT_EQUAL(1, tele_trap->metric_data_.size());
    TEST_ASSERT_EQUAL(1, tele_trap->metric_data_[12].count_);
    TEST_ASSERT_GREATER_THAN(1, tele_trap->metric_data_[12].duration_);

    TEST_ASSERT_EQUAL(1, tele_trap->log_lines_.size());
    auto& log_line = tele_trap->log_lines_.front();
    TEST_ASSERT_EQUAL(7, log_line.size());
    TEST_ASSERT_EQUAL_STRING("12", log_line[0].c_str());
    TEST_ASSERT_EQUAL_STRING("kDebug", log_line[2].c_str());
    TEST_ASSERT_EQUAL_STRING("TestObj", log_line[3].c_str());
    TEST_ASSERT_EQUAL_STRING(Format("test-tele.cpp:{}", remember_line).c_str(),
                             log_line[4].c_str());
    TEST_ASSERT_EQUAL_STRING("Test", log_line[5].c_str());
    TEST_ASSERT_EQUAL_STRING("message 12", log_line[6].c_str());
  }

  {
    // only metrics
    using Sink = TeleSink<tele_configuration::ConfigProvider<
        true, true, false, false, false, false, false, false>>;
    auto tele_trap = std::make_shared<tele_configuration::TeleTrap>();

    Sink::Instance().SetTrap(tele_trap);
    {
      auto t = Tele<Sink, Sink::TeleConfig<Level::kDebug, TestTag.module.id>>{
          Sink::Instance(), TestTag, Level{Level::kDebug}, __FILE__, __LINE__,
          "message {}",     12};
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    TEST_ASSERT_EQUAL(1, tele_trap->metric_data_.size());
    TEST_ASSERT_EQUAL(1, tele_trap->metric_data_[12].count_);
    TEST_ASSERT_GREATER_THAN(1, tele_trap->metric_data_[12].duration_);

    TEST_ASSERT(tele_trap->log_lines_.empty());
  }
  {
    // only count
    using Sink = TeleSink<tele_configuration::ConfigProvider<
        true, false, false, false, false, false, false, false>>;
    auto tele_trap = std::make_shared<tele_configuration::TeleTrap>();
    Sink::Instance().SetTrap(tele_trap);
    {
      auto t = Tele<Sink, Sink::TeleConfig<Level::kDebug, TestTag.module.id>>{
          Sink::Instance(), TestTag, Level{Level::kDebug}, __FILE__, __LINE__,
          "message {}",     12};

      TEST_ASSERT_EQUAL(1, sizeof(t));
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    TEST_ASSERT_EQUAL(1, tele_trap->metric_data_.size());
    TEST_ASSERT_EQUAL(1, tele_trap->metric_data_[12].count_);
    TEST_ASSERT_EQUAL(0, tele_trap->metric_data_[12].duration_);

    TEST_ASSERT(tele_trap->log_lines_.empty());
  }
  {
    // nothing
    using Sink = TeleSink<tele_configuration::ConfigProvider<
        false, false, false, false, false, false, false, false>>;
    auto tele_trap = std::make_shared<tele_configuration::TeleTrap>();

    TEST_ASSERT(tele_trap->log_lines_.empty());
  }
  {
    // nothing
    using Sink = TeleSink<tele_configuration::ConfigProvider<
        false, false, false, false, false, false, false, false>>;
    auto tele_trap = std::make_shared<tele_configuration::TeleTrap>();

    Sink::Instance().SetTrap(tele_trap);
    {
      auto t = Tele<Sink, Sink::TeleConfig<Level::kDebug, TestTag.module.id>>{
          Sink::Instance(), TestTag, Level{Level::kDebug}, __FILE__, __LINE__,
          "message {}",     12};

      TEST_ASSERT_EQUAL(1, sizeof(t));
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    TEST_ASSERT(tele_trap->metric_data_.empty());
    TEST_ASSERT(tele_trap->log_lines_.empty());
  }
  {
    // print only index log level, module and text
    using Sink =
        TeleSink<tele_configuration::ConfigProvider<false, false, true, false,
                                                    true, false, true, false>>;
    auto tele_trap = std::make_shared<tele_configuration::TeleTrap>();

    Sink::Instance().SetTrap(tele_trap);
    {
      auto t = Tele<Sink, Sink::TeleConfig<Level::kDebug, TestTag.module.id>>{
          Sink::Instance(), TestTag, Level{Level::kDebug}, __FILE__, __LINE__,
          "message {}",     12};

      TEST_ASSERT_EQUAL(1, sizeof(t));
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    TEST_ASSERT(tele_trap->metric_data_.empty());

    TEST_ASSERT_EQUAL(1, tele_trap->log_lines_.size());
    auto& log_line = tele_trap->log_lines_.front();
    TEST_ASSERT_EQUAL(4, log_line.size());
    TEST_ASSERT_EQUAL_STRING("12", log_line[0].c_str());
    TEST_ASSERT_EQUAL_STRING("kDebug", log_line[1].c_str());
    TEST_ASSERT_EQUAL_STRING("TestObj", log_line[2].c_str());
    TEST_ASSERT_EQUAL_STRING("Test", log_line[3].c_str());
  }
}

void test_TeleProxyTrap() {
  using ProxyTeleTrap =
      ProxyTrap<tele_configuration::TeleTrap, tele_configuration::TeleTrap>;

  auto first_trap = std::make_shared<tele_configuration::TeleTrap>();
  auto second_trap = std::make_shared<tele_configuration::TeleTrap>();
  auto proxy_tele_trap =
      std::make_shared<ProxyTeleTrap>(first_trap, second_trap);

  SinkType::Instance().SetTrap(proxy_tele_trap);

  {
    AE_TELE_DEBUG(Test1);
    AE_TELE_DEBUG(Test1, "format {}", 12);
    AE_TELE_INFO(Test2, "format {}", 24);
    AE_TELE_INFO(Test2, "format {}", 48);
  }

  auto& metrics = first_trap->metric_data_[Test1.offset + TestObj.index_start];

  TEST_ASSERT_EQUAL(2, metrics.count_);

  auto d = second_trap->metric_data_[Test1.offset + TestObj.index_start];
  TEST_ASSERT_EQUAL(metrics.count_, d.count_);
}

void test_MergeStatisticsTrap() {
  using Sink =
      TeleSink<tele_configuration::ConfigProvider<true, true, true, false,
                                                  false, false, false, false>>;
#undef TELE_SINK
#define TELE_SINK Sink
  auto statistics_trap1 = std::make_shared<statistics::StatisticsTrap>();

  Sink::Instance().SetTrap(statistics_trap1);
  {
    AE_TELE_DEBUG(Test1);
    AE_TELE_INFO(Test1);
    AE_TELE_WARNING(Test1);
    AE_TELE_ERROR(Test1);
    AE_TELE_DEBUG(Test2);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  auto logs1 = std::get<statistics::RuntimeLog>(
                   *statistics_trap1->statistics_store.log_store())
                   .logs;

  auto statistics_trap2 = std::make_shared<statistics::StatisticsTrap>();
  statistics_trap2->MergeStatistics(*statistics_trap1);

  auto& logs2 = std::get<statistics::RuntimeLog>(
                    *statistics_trap2->statistics_store.log_store())
                    .logs;

  TEST_ASSERT_EQUAL(logs1.size(), logs2.size());
  auto it1 = std::begin(logs1);
  auto it2 = std::begin(logs2);
  for (; it1 != std::end(logs1); ++it1, ++it2) {
    TEST_ASSERT_EQUAL_CHAR_ARRAY(it1->data(), it2->data(), it1->size());
  }

  auto& metrics1 = statistics_trap1->statistics_store.metrics_store().metrics;
  auto& metrics2 = statistics_trap2->statistics_store.metrics_store().metrics;

  TEST_ASSERT_EQUAL(metrics1.size(), metrics2.size());
  auto mit1 = std::begin(metrics1);
  auto mit2 = std::begin(metrics2);
  for (; mit1 != std::end(metrics1); ++mit1, ++mit2) {
    TEST_ASSERT_EQUAL(mit1->first, mit2->first);
    TEST_ASSERT_EQUAL(mit1->second.invocations_count,
                      mit2->second.invocations_count);
    TEST_ASSERT_EQUAL(mit1->second.max_duration, mit2->second.max_duration);
    TEST_ASSERT_EQUAL(mit1->second.min_duration, mit2->second.min_duration);
    TEST_ASSERT_EQUAL(mit1->second.sum_duration, mit2->second.sum_duration);
  }
  // switch to trap 2
  Sink::Instance().SetTrap(statistics_trap2);
  {
    AE_TELE_DEBUG(Test1);
    AE_TELE_DEBUG(Test2);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  // new logs added
  TEST_ASSERT_NOT_EQUAL(logs1.size(), logs2.size());
  // but no new metrics
  TEST_ASSERT_EQUAL(metrics1.size(), metrics2.size());
  auto new_mit1 = std::begin(metrics1);
  auto new_mit2 = std::begin(metrics2);
  for (; new_mit1 != std::end(metrics1); ++new_mit1, ++new_mit2) {
    TEST_ASSERT_EQUAL(new_mit1->first, new_mit2->first);
    TEST_ASSERT_NOT_EQUAL(new_mit1->second.invocations_count,
                          new_mit2->second.invocations_count);
    TEST_ASSERT_NOT_EQUAL(new_mit1->second.sum_duration,
                          new_mit2->second.sum_duration);
  }

#undef TELE_SINK
#define TELE_SINK SinkType
}
void test_EnvTele() { AE_TELE_ENV(); }
}  // namespace ae::tele::test_tele

int main() {
  UNITY_BEGIN();

  RUN_TEST(ae::tele::test_tele::test_Register);
  RUN_TEST(ae::tele::test_tele::test_SimpleTeleWithDuration);
  RUN_TEST(ae::tele::test_tele::test_TeleConfigurations);
  RUN_TEST(ae::tele::test_tele::test_TeleProxyTrap);
  RUN_TEST(ae::tele::test_tele::test_MergeStatisticsTrap);
  RUN_TEST(ae::tele::test_tele::test_EnvTele);

  return UNITY_END();
}
