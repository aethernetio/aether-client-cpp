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

#ifndef AETHER_UAP_UAP_H_
#define AETHER_UAP_UAP_H_

#include "aether/common.h"
#include "aether/obj/obj.h"
#include "aether/events/events.h"
#include "aether/actions/action_ptr.h"
#include "aether/actions/timer_action.h"

namespace ae {
class Aether;

class Uap final : public Obj {
  AE_OBJECT(Uap, Obj, 0)
  Uap() = default;

 public:
  using SleepEvent = Event<void(TimePoint sleep_until)>;

  Uap(ObjProp prop, ObjPtr<Aether> aether, Duration interval);

  AE_OBJECT_REFLECT(AE_MMBRS(aether_, interval_))

  /**
   * \brief User notifies UAP - busyness logic is ready to sleep.
   */
  void SleepReady();

  /**
   * \brief Uap notifies user - all systems are ready to sleep up to sleep_until
   * time.
   */
  SleepEvent::Subscriber sleep_event();

  void ChangeInterval(Duration interval);

 private:
  void UpdateTimer();

  Event<void(TimePoint sleep_until)> sleep_event_;

  ObjPtr<Aether> aether_;
  Duration interval_;

  OwnActionPtr<TimerAction> timer_action_;
};
}  // namespace ae

#endif  // AETHER_UAP_UAP_H_
