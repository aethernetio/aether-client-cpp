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

#include "led_button_esp.h"

#if (defined(BUTTON_ESP))
#  include "aether/tele/tele.h"

namespace ae {

LedButtonEsp::LedButtonEsp(ActionContext action_context)
    : Action{action_context}, button_timeout_{std::chrono::milliseconds{100}} {
  this->InitIOPriv();
}

LedButtonEsp::~LedButtonEsp() {}

TimePoint LedButtonEsp::Update(TimePoint current_time) {
  bool res{false};

  if (current_time > prev_time_ + button_timeout_) {
    prev_time_ = current_time;
    res = GetKeyPriv(BUT_PIN, BUT_MASK);
    if (res != button_state_) {
      AE_TELED_INFO("Button state changed");
      button_state_ = res;
      this->ResultRepeat(*this);
    }
  }
  return current_time;
}

bool LedButtonEsp::GetKey(void) {
  bool res = button_state_;

  return res;
}

void LedButtonEsp::SetLed(bool led_state) { SetLedPriv(LED_PIN, led_state); }

void LedButtonEsp::InitIOPriv(void) {
  gpio_reset_pin(LED_PIN);
  gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);
  gpio_set_direction(BUT_PIN, GPIO_MODE_INPUT);
}

bool LedButtonEsp::GetKeyPriv(uint16_t key_pin, uint16_t key_mask) {
  bool res;

  res = gpio_get_level(static_cast<gpio_num_t>(key_pin));

  return res;
}

void LedButtonEsp::SetLedPriv(uint16_t led_pin, bool led_state) {
  gpio_set_level(static_cast<gpio_num_t>(led_pin), led_state);
}
}  // namespace ae

#endif
