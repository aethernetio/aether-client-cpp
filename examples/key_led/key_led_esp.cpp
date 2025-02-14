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

#include "key_led_esp.h"

#if defined(KEY_LED_ESP)
#  include "aether/tele/tele.h"

namespace ae {

KeyLedEsp::KeyLedEsp(ActionContext action_context)
    : Action{action_context}, key_timeout_{std::chrono::milliseconds{100}} {
  this->InitIOPriv();
}

KeyLedEsp::~KeyLedEsp() {}

TimePoint KeyLedEsp::Update(TimePoint current_time) {
  bool res{false};

  if (current_time > prev_time_ + key_timeout_) {
    prev_time_ = current_time;
    res = GetKeyPriv(BUT_PIN, BUT_MASK);
    if (res != key_state_) {
      AE_TELED_INFO("Key state changed");
      key_state_ = res;
      this->ResultRepeat(*this);
    }
  }
  return current_time;
}

bool KeyLedEsp::GetKey(void) {
  bool res = key_state_;

  return res;
}

void KeyLedEsp::SetLed(bool led_state) { SetLedPriv(LED_PIN, led_state); }

void KeyLedEsp::InitIOPriv(void) {
  gpio_reset_pin(LED_PIN);
  gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
  gpio_set_direction(BUT_PIN, GPIO_MODE_INPUT);
}

bool KeyLedEsp::GetKeyPriv(uint16_t key_pin, uint16_t key_mask) {
  bool res;

  res = gpio_get_level(static_cast<gpio_num_t>(key_pin));

  return res;
}

void KeyLedEsp::SetLedPriv(uint16_t led_pin, bool led_state) {
  gpio_set_level(static_cast<gpio_num_t>(led_pin), led_state);
}
}  // namespace ae

#endif
