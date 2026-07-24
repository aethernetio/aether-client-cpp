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

#ifndef AETHER_TELE_TRAPS_TELE_STATISTICS_H_
#define AETHER_TELE_TRAPS_TELE_STATISTICS_H_

#include <memory>

#include "aether/tele/traps/statistics_trap.h"

#include "aether/config.h"
#include "aether/obj/obj.h"

namespace ae {
class TeleStatistics : public Obj {
  AE_OBJECT(TeleStatistics, Obj, 0)

  TeleStatistics() = default;

 public:
#ifdef AE_DISTILLATION
  explicit TeleStatistics(ObjProp prop);
#endif  // AE_DISTILLATION

  AE_OBJECT_REFLECT()

#if AE_TELE_ENABLED && AE_TELE_LOG_TO_STATISTICS
  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_, *trap_);
    OnLoaded();
  }
  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(base_, *trap_);
  }
#endif

#if AE_TELE_ENABLED && AE_TELE_LOG_TO_STATISTICS
  using Trap = tele::StatisticsTrap<AE_STATISTICS_MAX_SIZE>;

  std::shared_ptr<Trap> const& trap();

 private:
  void OnLoaded();
  std::shared_ptr<Trap> trap_ = std::make_shared<Trap>();
#endif
};
}  // namespace ae

#endif  // AETHER_TELE_TRAPS_TELE_STATISTICS_H_
