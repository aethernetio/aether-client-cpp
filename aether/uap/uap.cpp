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

#include "aether/uap/uap.h"

#include "aether/aether.h"

#include "aether/tele/tele.h"

namespace ae {
Uap::Uap(ObjProp prop, ObjPtr<Aether> aether, Duration interval)
    : Obj{prop}, aether_{std::move(aether)}, interval_{interval} {
  UpdateTimer();
}

void Uap::SleepReady() {
  // TODO: add checks if all buffers are empty
  auto current_time = Now();
  auto time_elapsed = timer_action_->elapsed(current_time);
  auto diff_time = interval_ - time_elapsed;
  auto sleep_until = current_time + diff_time;

  AE_TELED_DEBUG("Sleep even until {:%Y-%m-%d %H:%M:%S}", sleep_until);
  sleep_event_.Emit(sleep_until);
}

Uap::SleepEvent::Subscriber Uap::sleep_event() {
  return EventSubscriber{sleep_event_};
}

void Uap::ChangeInterval(Duration interval) {
  interval_ = interval;
  UpdateTimer();
}

void Uap::UpdateTimer() {
  timer_action_.reset();
  timer_action_ = OwnActionPtr<TimerAction>{*aether_, interval_};
  // run timer for interval, and update itself on elapsed
  timer_action_->StatusEvent().Subscribe(OnResult{[this]() { UpdateTimer(); }});
}

}  // namespace ae
