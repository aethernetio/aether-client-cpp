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

#include "aether/tele_statistics.h"

#include "aether/tele.h"

namespace ae {
#ifdef AE_DISTILLATION
TeleStatistics::TeleStatistics(ObjProp prop) : Obj{prop} {}
#endif

#if AE_TELE_ENABLED && AE_TELE_LOG_TO_STATISTICS
auto TeleStatistics::trap() -> std::shared_ptr<Trap> const& { return trap_; }

void TeleStatistics::OnLoaded() {
  auto& sink = TELE_SINK::Instance();
  auto const& trap = sink.trap();
  if (!trap) {
    sink.SetTrap(trap_);
    return;
  }
  // !NOTICE it's error if AE_TELE_LOG_TO_STATISTICS is defined to 1 but actual
  // trap is set to something different
#  if AE_TELE_LOG_CONSOLE && AE_TELE_LOG_TO_STATISTICS
  // if proxy trap is used
  auto proxy = std::static_pointer_cast<
      ae::tele::ProxyTrap<ae::tele::IoStreamTrap,
                          ae::tele::StatisticsTrap<AE_STATISTICS_MAX_SIZE>>>(
      trap);
  auto const& current_statistics = proxy->second;
  trap_->MergeStatistics(*current_statistics);
  proxy->second = trap_;
#  elif AE_TELE_LOG_TO_STATISTICS
  auto current_statistics = std::static_pointer_cast<
      ae::tele::StatisticsTrap<AE_STATISTICS_MAX_SIZE>>(trap);
  trap_->MergeStatistics(*current_statistics);
  sink.SetTrap(trap_);
#  endif
}

#endif
}  // namespace ae
