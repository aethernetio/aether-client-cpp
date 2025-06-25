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

#include "aether/adapters/modems/bg95_at_modem.h"
#include "aether/adapters/modems/serial_ports/serial_port_factory.h"
#include "aether/adapters/adapter_tele.h"

namespace ae {

Bg95AtModem::Bg95AtModem(ModemInit modem_init) : modem_init_(modem_init) {
  serial_ = SerialPortFactory::CreatePort(modem_init_.serial_init);
};

void Bg95AtModem::Init() {
  //int err{0};
}

void Bg95AtModem::Setup() {
  //int err{0};
}

void Bg95AtModem::Stop() {
  //int err{0};
}

//================================private members================================//
int SetWCDMATxPower(kModemBand band, std::int16_t power){
  int err{0};

  AE_TELE_ERROR(kAdapterSerialNotOpen, "Band {}", band);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Power {}", power);

  return err;
}

} /* namespace ae */
