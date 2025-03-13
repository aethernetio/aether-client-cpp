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

#include "aether/type_traits.h"

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

using SinkType =
    ae::tele::TeleSink<ae::tele::IoStreamTrap, ae::tele::ConfigProvider>;

#define TELE_SINK SinkType

static ae::RcPtr<ae::tele::IoStreamTrap> trap;

void setUp() {
  trap = ae::MakeRcPtr<ae::tele::IoStreamTrap>(std::cout);
  SinkType::InitSink(trap);
}

void tearDown() { trap.Reset(); }

AE_TELE_MODULE(TestObj, 12);

AE_TAG_INDEXED(One, TestObj, 1)
AE_TAG(Two, TestObj)
AE_TAG(Three, TestObj)
// AE_TAG_INDEXED(Three2, TestObj, 3)  // must not compile
// duplicated!
AE_TAG_INDEXED(Twelve, TestObj, 12)
// AE_TAG_INDEXED(Twelve2, TestObj, 12)  // must not compile
// duplicated!

AE_TAG(Test1, TestObj)
AE_TAG(Test2, TestObj)
AE_TAG(Test3, TestObj)

namespace ae::tele::test_tele {
void test_Register() {
  TEST_ASSERT_EQUAL(1, One.index);
  TEST_ASSERT_EQUAL(2, Two.index);
  TEST_ASSERT_EQUAL(3, Three.index);
  TEST_ASSERT_EQUAL(12, Twelve.index);
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
  auto& metrics = trap->metrics_[Test1.index];

  TEST_ASSERT_EQUAL(2, metrics.invocations_count_);
}

namespace tele_configuration {
struct TeleTrap {
  struct MetricData {
    std::uint32_t count_{};
    std::uint32_t duration_{};
  };

  std::list<std::vector<std::string>> log_lines_;
  std::map<std::size_t, MetricData> metric_data_;

  struct LogStream {
    std::vector<std::string>& line_;

    void index(std::uint32_t index) {
      auto str = std::to_string(index);
      put(str.c_str(), str.size());
    }
    void start_time(ae::TimePoint const& start) {
      auto str = std::to_string(start.time_since_epoch().count());
      put(str.c_str(), str.size());
    }
    void level(ae::tele::Level::underlined_t level) {
      auto l_str = ae::Format("{}", ae::tele::Level{level});
      put(l_str.c_str(), l_str.size());
    }
    void module(ae::tele::Module module) {
      auto m_str = ae::Format("{}", ae::tele::Module{module});
      put(m_str.c_str(), m_str.size());
    }
    void file(std::string_view file) {
      auto pos = file.find_last_of("/\\");
      if (pos == std::string_view::npos) {
        file = "UNKOWN FILE";
      }
      file = file.substr(pos + 1, file.size() - pos);
      put(file.data(), file.size());
    }
    void line(std::uint32_t line) {
      auto str = std::to_string(line);
      put(str.c_str(), str.size());
    }
    void name(std::string_view name) { put(name.data(), name.size()); }

    template <typename... TArgs>
    void blob(std::string_view format, TArgs&&... args) {
      auto str = ae::Format(format, std::forward<TArgs>(args)...);
      put(str.c_str(), str.size());
    }

    void put(const char* log_data, std::size_t size) {
      line_.emplace_back(log_data, size);
    }
  };

  struct MetricStream {
    MetricData& data_;
    void add_count(uint32_t count) { data_.count_ += count; }
    void add_duration(uint32_t duration) { data_.duration_ += duration; }
  };

  LogStream log_stream(ae::tele::Declaration /* decl */) {
    return {log_lines_.emplace_back()};
  }
  MetricStream metric_stream(ae::tele::Declaration decl_) {
    return {metric_data_[decl_.index]};
  }
};

template <bool Count = true, bool Time = true, bool Index = true,
          bool StartTime = true, bool LevelModule = true, bool Location = true,
          bool Text = true, bool Blob = true>
struct ConfigProvider {
  template <bool... args>
  struct TeleConfig {
    static constexpr bool kCountMetrics = ArgAt<0, args...>();
    static constexpr bool kTimeMetrics = ArgAt<1, args...>();
    static constexpr bool kIndexLogs = ArgAt<2, args...>();
    static constexpr bool kStartTimeLogs = ArgAt<3, args...>();
    static constexpr bool kLevelModuleLogs = ArgAt<4, args...>();
    static constexpr bool kLocationLogs = ArgAt<5, args...>();
    static constexpr bool kNameLogs = ArgAt<6, args...>();
    static constexpr bool kBlobLogs = ArgAt<7, args...>();
  };

  template <bool... args>
  struct EnvConfig {
    static constexpr bool kCompiler = ArgAt<0, args...>();
    static constexpr bool kPlatformType = ArgAt<1, args...>();
    static constexpr bool kCompilationOptions = ArgAt<2, args...>();
    static constexpr bool kLibraryVersion = ArgAt<3, args...>();
    static constexpr bool kApiVersion = ArgAt<4, args...>();
    static constexpr bool kCpuType = ArgAt<5, args...>();
  };

  template <ae::tele::Level::underlined_t level, std::uint32_t module>
  using StaticTeleConfig = TeleConfig<Count, Time, Index, StartTime,
                                      LevelModule, Location, Text, Blob>;

  using StaticEnvConfig = EnvConfig<true, true, true, true, true>;
};
}  // namespace tele_configuration

void test_TeleConfigurations() {
  constexpr auto TestTag = Tag{12, MLog, "Test"};
  {
    // all enabled
    using Sink = TeleSink<tele_configuration::TeleTrap,
                          tele_configuration::ConfigProvider<>>;
    auto tele_trap = MakeRcPtr<tele_configuration::TeleTrap>();

    Sink::InitSink(tele_trap);
    int remember_line = __LINE__ + 4;
    {
      auto t =
          Tele<Sink, Sink::TeleConfig<Level::kDebug, TestTag.module.value>>{
              Sink::Instance(), TestTag, Level::kDebug, __FILE__, __LINE__,
              "message {}",     12};

      TEST_ASSERT_EQUAL(16, sizeof(t));
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    TEST_ASSERT_EQUAL(1, tele_trap->metric_data_.size());
    TEST_ASSERT_EQUAL(1, tele_trap->metric_data_[12].count_);
    TEST_ASSERT_GREATER_THAN(1, tele_trap->metric_data_[12].duration_);

    TEST_ASSERT_EQUAL(1, tele_trap->log_lines_.size());
    auto& log_line = tele_trap->log_lines_.front();
    TEST_ASSERT_EQUAL(8, log_line.size());
    TEST_ASSERT_EQUAL_STRING("12", log_line[0].c_str());
    TEST_ASSERT_EQUAL_STRING("kDebug", log_line[2].c_str());
    TEST_ASSERT_EQUAL_STRING("MLog", log_line[3].c_str());
    TEST_ASSERT_EQUAL_STRING("test-tele.cpp", log_line[4].c_str());
    TEST_ASSERT_EQUAL_STRING(std::to_string(remember_line).c_str(),
                             log_line[5].c_str());
    TEST_ASSERT_EQUAL_STRING("Test", log_line[6].c_str());
    TEST_ASSERT_EQUAL_STRING("message 12", log_line[7].c_str());
  }

  {
    // only metrics
    using Sink =
        TeleSink<tele_configuration::TeleTrap,
                 tele_configuration::ConfigProvider<
                     true, true, false, false, false, false, false, false>>;
    auto tele_trap = ae::MakeRcPtr<tele_configuration::TeleTrap>();

    Sink::InitSink(tele_trap);
    {
      auto t =
          Tele<Sink, Sink::TeleConfig<Level::kDebug, TestTag.module.value>>{
              Sink::Instance(), TestTag, Level::kDebug, __FILE__, __LINE__,
              "message {}",     12};

      TEST_ASSERT_EQUAL(16, sizeof(t));
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    TEST_ASSERT_EQUAL(1, tele_trap->metric_data_.size());
    TEST_ASSERT_EQUAL(1, tele_trap->metric_data_[12].count_);
    TEST_ASSERT_GREATER_THAN(1, tele_trap->metric_data_[12].duration_);

    TEST_ASSERT(tele_trap->log_lines_.empty());
  }
  {
    // only count
    using Sink =
        TeleSink<tele_configuration::TeleTrap,
                 tele_configuration::ConfigProvider<
                     true, false, false, false, false, false, false, false>>;
    auto tele_trap = ae::MakeRcPtr<tele_configuration::TeleTrap>();
    Sink::InitSink(tele_trap);
    {
      auto t =
          Tele<Sink, Sink::TeleConfig<Level::kDebug, TestTag.module.value>>{
              Sink::Instance(), TestTag, Level::kDebug, __FILE__, __LINE__,
              "message {}",     12};

      TEST_ASSERT_EQUAL(16, sizeof(t));
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    TEST_ASSERT_EQUAL(1, tele_trap->metric_data_.size());
    TEST_ASSERT_EQUAL(1, tele_trap->metric_data_[12].count_);
    TEST_ASSERT_EQUAL(0, tele_trap->metric_data_[12].duration_);

    TEST_ASSERT(tele_trap->log_lines_.empty());
  }
  {
    // nothing
    using Sink =
        TeleSink<tele_configuration::TeleTrap,
                 tele_configuration::ConfigProvider<
                     false, false, false, false, false, false, false, false>>;
    auto tele_trap = ae::MakeRcPtr<tele_configuration::TeleTrap>();

    TEST_ASSERT(tele_trap->log_lines_.empty());
  }
  {
    // nothing
    using Sink =
        TeleSink<tele_configuration::TeleTrap,
                 tele_configuration::ConfigProvider<
                     false, false, false, false, false, false, false, false>>;
    auto tele_trap = ae::MakeRcPtr<tele_configuration::TeleTrap>();

    Sink::InitSink(tele_trap);
    {
      auto t =
          Tele<Sink, Sink::TeleConfig<Level::kDebug, TestTag.module.value>>{
              Sink::Instance(), TestTag, Level::kDebug, __FILE__, __LINE__,
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
        TeleSink<tele_configuration::TeleTrap,
                 tele_configuration::ConfigProvider<false, false, true, false,
                                                    true, false, true, false>>;
    auto tele_trap = ae::MakeRcPtr<tele_configuration::TeleTrap>();

    Sink::InitSink(tele_trap);
    {
      auto t =
          Tele<Sink, Sink::TeleConfig<Level::kDebug, TestTag.module.value>>{
              Sink::Instance(), TestTag, Level::kDebug, __FILE__, __LINE__,
              "message {}",     12};

      TEST_ASSERT_EQUAL(2, sizeof(t));
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    TEST_ASSERT(tele_trap->metric_data_.empty());

    TEST_ASSERT_EQUAL(1, tele_trap->log_lines_.size());
    auto& log_line = tele_trap->log_lines_.front();
    TEST_ASSERT_EQUAL(4, log_line.size());
    TEST_ASSERT_EQUAL_STRING("12", log_line[0].c_str());
    TEST_ASSERT_EQUAL_STRING("kDebug", log_line[1].c_str());
    TEST_ASSERT_EQUAL_STRING("MLog", log_line[2].c_str());
    TEST_ASSERT_EQUAL_STRING("Test", log_line[3].c_str());
  }
}

void test_TeleProxyTrap() {
  using ProxyTeleTrap =
      ProxyTrap<tele_configuration::TeleTrap, ae::tele::IoStreamTrap>;

  using Sink = TeleSink<ProxyTeleTrap, ConfigProvider>;
  auto tele_trap = ae::MakeRcPtr<tele_configuration::TeleTrap>();
  auto proxy_tele_trap = ae::MakeRcPtr<ProxyTeleTrap>(tele_trap, ::trap);

  Sink::InitSink(proxy_tele_trap);

#undef TELE_SINK
#define TELE_SINK Sink
  {
    AE_TELE_DEBUG(Test1);
    AE_TELE_DEBUG(Test1, "format {}", 12);
    AE_TELE_INFO(Test2, "format {}", 24);
    AE_TELE_INFO(Test2, "format {}", 48);
  }

  auto& metrics = ::trap->metrics_[Test1.index];

  TEST_ASSERT_EQUAL(2, metrics.invocations_count_);

  auto d = tele_trap->metric_data_[Test1.index];
  TEST_ASSERT_EQUAL(metrics.invocations_count_, d.count_);
#undef TELE_SINK
#define TELE_SINK SinkType
}

void test_MergeStatisticsTrap() {
  using Sink =
      TeleSink<statistics::StatisticsTrap,
               tele_configuration::ConfigProvider<true, true, true, false,
                                                  false, false, false, false>>;
#undef TELE_SINK
#define TELE_SINK Sink
  auto statistics_trap1 = ae::MakeRcPtr<statistics::StatisticsTrap>();

  Sink::InitSink(statistics_trap1);
  {
    AE_TELE_DEBUG(Test1);
    AE_TELE_INFO(Test1);
    AE_TELE_WARNING(Test1);
    AE_TELE_ERROR(Test1);
    AE_TELE_DEBUG(Test2);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  auto statistics_trap2 = ae::MakeRcPtr<statistics::StatisticsTrap>();
  statistics_trap2->MergeStatistics(*statistics_trap1);

  auto& logs1 = statistics_trap1->statistics_store_.Get()->logs().logs;
  auto& logs2 = statistics_trap2->statistics_store_.Get()->logs().logs;

  TEST_ASSERT_EQUAL(logs1.size(), logs2.size());
  auto it1 = std::begin(logs1);
  auto it2 = std::begin(logs2);
  for (; it1 != std::end(logs1); ++it1, ++it2) {
    TEST_ASSERT_EQUAL_CHAR_ARRAY(it1->data(), it2->data(), it1->size());
  }

  auto& metrics1 = statistics_trap1->statistics_store_.Get()->metrics().metrics;
  auto& metrics2 = statistics_trap2->statistics_store_.Get()->metrics().metrics;

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
  Sink::InitSink(statistics_trap2);
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
