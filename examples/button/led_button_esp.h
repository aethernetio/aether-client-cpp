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

#ifndef BUTTON_LED_BUTTON_ESP_H_
#define BUTTON_LED_BUTTON_ESP_H_

#if (defined(ESP_PLATFORM))
#  define BUTTON_ESP

#  include <cstdint>
#  include "aether/actions/action.h"
#  include "aether/actions/action_context.h"

#  include "freertos/FreeRTOS.h"
#  include "freertos/task.h"
#  include "driver/gpio.h"

#  define LED_PIN GPIO_NUM_5
#  define BUT_PIN GPIO_NUM_4
#  define BUT_MASK 0xFFFF

namespace ae {
class LedButtonEsp : public Action<LedButtonEsp> {
 public:
  LedButtonEsp(ActionContext action_context);
  ~LedButtonEsp() override;
  TimePoint Update(TimePoint current_time) override;
  bool GetKey(void);
  void SetLed(bool led_state);

 private:
  void InitIOPriv(void);
  bool GetKeyPriv(uint16_t key_pin, uint16_t key_mask);
  void SetLedPriv(uint16_t led_pin, bool led_state);

  TimePoint prev_time_;
  TimePoint::duration button_timeout_;
  bool button_state_{false};
};
}  // namespace ae

#endif
#endif  // BUTTON_LED_BUTTON_ESP_H_