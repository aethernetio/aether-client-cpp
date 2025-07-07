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

#include "aether/obj/obj.h"
#include "aether/ptr/rc_ptr.h"
#include "aether/reflect/reflect.h"

#include "aether/tele/tele.h"
#include "aether/tele/traps/statistics_trap.h"

namespace ae::tele {
class TeleStatistics : public Obj {
  AE_OBJECT(TeleStatistics, Obj, 0)

  TeleStatistics() = default;

 public:
#ifdef AE_DISTILLATION
  explicit TeleStatistics(Domain* domain);
#endif  // AE_DISTILLATION

#if AE_TELE_ENABLED
  AE_OBJECT_REFLECT(AE_MMBR(trap_))

  template <typename Dnv>
  void Load(CurrentVersion, Dnv& dnv) {
    dnv(base_, *trap_);
  }
  template <typename Dnv>
  void Save(CurrentVersion, Dnv& dnv) const {
    dnv(base_, *trap_);
  }
#else
  AE_OBJECT_REFLECT()
#endif

#if AE_TELE_ENABLED
  std::shared_ptr<statistics::StatisticsTrap> const& trap();

 private:
  std::shared_ptr<statistics::StatisticsTrap> trap_ =
      std::make_shared<statistics::StatisticsTrap>();
#endif
};
}  // namespace ae::tele

#endif  // AETHER_TELE_TRAPS_TELE_STATISTICS_H_
