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

void IModemDriver::Init() {}
void IModemDriver::Start() {}
void IModemDriver::Stop() {}
void IModemDriver::OpenNetwork(std::int8_t& /*connect_index*/,
                               ae::Protocol const /*protocol*/,
                               const std::string /*host*/,
                               const std::uint16_t /*port*/) {}
void IModemDriver::CloseNetwork(std::int8_t const /*connect_index*/) {}
void IModemDriver::WritePacket(std::int8_t const /*connect_index*/,
                               std::vector<uint8_t> const& /*data*/) {}
void IModemDriver::ReadPacket(std::int8_t const /*connect_index*/,
                              std::vector<std::uint8_t>& /*data*/,
                              std::int32_t const /*timeout*/) {}
void IModemDriver::PollSockets(std::int8_t const /*connect_index*/,
                               PollResult& /*results*/,
                               std::int32_t const /*timeout*/) {}
void IModemDriver::SetPowerSaveParam(ae::PowerSaveParam const& /*psp*/) {}
void IModemDriver::PowerOff() {}
ModemInit IModemDriver::GetModemInit() { return modem_init_; }

} /* namespace ae */
