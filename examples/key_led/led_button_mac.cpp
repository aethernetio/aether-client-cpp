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

#include "led_button_mac.h"

#if defined BUTTON_MAC

namespace ae {
LedButtonMac::LedButtonMac(ActionContext /* action_context */) {
  assert(false);
}

LedButtonMac::~LedButtonMac() = default;
TimePoint LedButtonMac::Update(TimePoint current_time) { return current_time; }

bool LedButtonMac::GetKey(void) { return false; }
void LedButtonMac::SetLed(bool /* led_state */) {}
void LedButtonMac::InitIOPriv(void) {}
bool LedButtonMac::GetKeyPriv(uint16_t /* key_pin */, uint16_t /* key_mask */) {
  return false;
}
void LedButtonMac::SetLedPriv(uint16_t /* led_pin */, bool /* led_state */) {}

}  // namespace ae

#endif
