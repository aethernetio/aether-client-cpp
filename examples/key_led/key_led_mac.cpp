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

#include "key_led_mac.h"

#if defined(KEY_LED_MAC)
#  include "aether/tele/tele.h"

namespace ae {
KeyLedMac::KeyLedMac(ActionContext /* action_context */) {
  assert(false);
}

KeyLedMac::~KeyLedMac() = default;
TimePoint KeyLedMac::Update(TimePoint current_time) { return current_time; }

bool KeyLedMac::GetKey(void) { return false; }
void KeyLedMac::SetLed(bool /* led_state */) {}
void KeyLedMac::InitIOPriv(void) {}
bool KeyLedMac::GetKeyPriv(uint16_t /* key_pin */, uint16_t /* key_mask */) {
  return false;
}
void KeyLedMac::SetLedPriv(uint16_t /* led_pin */, bool /* led_state */) {}

}  // namespace ae

#endif
