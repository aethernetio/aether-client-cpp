/*
 * Copyright 2025 Aethernet Inc.
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

#include "aether/modems/imodem_driver.h"

namespace ae {

IModemDriver::IModemDriver(ModemInit modem_init, Domain* domain)
    : Obj{domain}, modem_init_{std::move(modem_init)} {}

bool IModemDriver::Init() { return {}; }
bool IModemDriver::Start() { return {}; }
bool IModemDriver::Stop() { return {}; }
ConnectionIndex IModemDriver::OpenNetwork(ae::Protocol /*protocol*/,
                                          std::string const& /*host*/,
                                          std::uint16_t /*port*/) {
  return {};
}
void IModemDriver::CloseNetwork(ConnectionIndex /*connect_index*/) {}
void IModemDriver::WritePacket(ConnectionIndex /*connect_index*/,
                               DataBuffer const& /*data*/) {}
DataBuffer IModemDriver::ReadPacket(ConnectionIndex /*connect_index*/,
                                    Duration /*timeout*/) {
  return {};
}

bool IModemDriver::SetPowerSaveParam(ae::PowerSaveParam const& /*psp*/) {
  return {};
}
bool IModemDriver::PowerOff() { return {}; }

ModemInit IModemDriver::get_modem_init() { return modem_init_; }

} /* namespace ae */
