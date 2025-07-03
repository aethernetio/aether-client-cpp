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

#include "aether/tele/tele_init.h"

#include <utility>
#include <iostream>

#include "aether/config.h"

namespace ae::tele {

template <typename SelectedSink>
static void TeleSinkInit() {
#if AE_TELE_ENABLED
#  if AE_TELE_LOG_CONSOLE
  // create statistics and console traps and combine them into one by proxy trap
  auto console_trap = std::make_shared<IoStreamTrap>(std::cout);
  auto statistics_trap = std::make_shared<statistics::StatisticsTrap>();

  SelectedSink::Instance().SetTrap(
      std::make_shared<ProxyTrap<IoStreamTrap, statistics::StatisticsTrap>>(
          std::move(console_trap), std::move(statistics_trap)));
#  else
  // use just statistics trap
  SelectedSink::Instance().SetTrap(
      std::make_shared<statistics::StatisticsTrap>());
#  endif
#endif
}

template <typename SelectedSink>
static void TeleSinkReInit(
    [[maybe_unused]] TeleStatistics::ptr const& tele_statistics) {
#if AE_TELE_ENABLED
#  if AE_TELE_LOG_CONSOLE
  // statistics trap + print logs to iostream
  auto proxy_trap = std::static_pointer_cast<
      ProxyTrap<IoStreamTrap, statistics::StatisticsTrap>>(
      SelectedSink::Instance().trap());
  auto new_tele_trap = tele_statistics->trap();
  new_tele_trap->MergeStatistics(*proxy_trap->second);
  proxy_trap->second = new_tele_trap;
  SelectedSink::Instance().SetTrap(std::move(proxy_trap));
#  else
  // just statistics trap
  auto old_trap = std::static_pointer_cast<statistics::StatisticsTrap>(
      SelectedSink::Instance().trap());
  auto new_tele_trap = tele_statistics->trap();
  new_tele_trap->MergeStatistics(*old_trap);

  SelectedSink::Instance().SetTrap(std::move(new_tele_trap));
#  endif
#endif
}

void TeleInit::Init() { TeleSinkInit<TELE_SINK>(); }
void TeleInit::Init(TeleStatistics::ptr const& tele_statistics) {
  TeleSinkReInit<TELE_SINK>(tele_statistics);
}

}  // namespace ae::tele
