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

#include "key_led_win.h"

#if (defined(KEY_LED_WIN))

#  include "aether/tele/tele.h"

namespace ae {

KeyLedWin::KeyLedWin(ActionContext action_context)
    : Action{action_context}, key_timeout_{std::chrono::milliseconds{100}} {
  this->InitIOPriv();
}

KeyLedWin::~KeyLedWin() {}

TimePoint KeyLedWin::Update(TimePoint current_time) {
  bool res{false};

  if (current_time > prev_time_ + key_timeout_) {
    prev_time_ = current_time;
    res = GetKeyPriv(BUT_PIN, BUT_MASK);
    if (res != key_state_) {
      AE_TELED_INFO("Key state changed");
      key_state_ = res;
      Action::ResultRepeat(*this);
    }
  }
  return current_time;
}

bool KeyLedWin::GetKey(void) {
  bool res = key_state_;

  return res;
}

void KeyLedWin::SetLed(bool led_state) { SetLedPriv(LED_PIN, led_state); }

void KeyLedWin::InitIOPriv(void) {}

bool KeyLedWin::GetKeyPriv(uint16_t key_pin, uint16_t key_mask) {
  bool res;

  if (GetKeyState(key_pin) & key_mask) {
    // The key is currently pressed.
    // Perform some action...
    res = true;
  } else {
    res = false;
  }

  return res;
}

void KeyLedWin::SetLedPriv(uint16_t led_pin, bool led_state) {
  if (led_state) {
    // Simulate a key press
    keybd_event(led_pin, 0x45, KEYEVENTF_EXTENDEDKEY | 0, 0);
  } else {
    // Simulate a key release
    keybd_event(led_pin, 0x45, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
  }
}
}  // namespace ae

#endif
