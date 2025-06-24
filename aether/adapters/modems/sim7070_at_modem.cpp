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

#include "aether/adapters/modems/sim7070_at_modem.h"
#include "aether/adapters/modems/serial_ports/serial_port_factory.h"
#include "aether/adapters/adapter_tele.h"

namespace ae {

Sim7070AtModem::Sim7070AtModem(ModemInit modem_init) : 
  modem_init_(modem_init) {
  serial_ = SerialPortFactory::CreatePort(modem_init_.serial_init);
  };

void Sim7070AtModem::Init() {
  try {
    sendATCommand("AT");  // Checking the connection
    if (!waitForResponse("OK", std::chrono::milliseconds(1000))) {
      AE_TELE_ERROR(kAdapterSerialNotOpen, "No response from modem");
    }

    sendATCommand("ATE0");       // Turning off the echo
    sendATCommand("AT+CMEE=2");  // Enabling extended errors

    // Additional initialization commands...
    event_initiated_.notify();
  } catch (const std::exception& e) {
    event_error_.notify(e.what());
  }
}

void Sim7070AtModem::Setup() {
  try {
    // Configuring modem settings
    sendATCommand("AT+CFUN=1");  // Enabling full functionality
    sendATCommand("AT+CNMP=2");  // Preferred network mode: LTE
    sendATCommand("AT+CMNB=1");  // Preferred mode: CAT-M
  } catch (const std::exception& e) {
    event_error_.notify(e.what());
  }
}

void Sim7070AtModem::Stop() {
  try {
    sendATCommand("AT+CPOWD=1");  // Turning off the modem correctly
  } catch (const std::exception& e) {
    event_error_.notify(e.what());
  }
}

EventSubscriberModem<>& Sim7070AtModem::initiated() {
  return event_initiated_.subscriber();
}

EventSubscriberModem<const std::string&>& Sim7070AtModem::errorOccurred() {
  return event_error_.subscriber();
}

void Sim7070AtModem::sendATCommand(const std::string& command) {
  std::vector<uint8_t> data(command.begin(), command.end());
  data.push_back('\r');  // Adding a carriage return symbol
  serial_->WriteData(data);
}

bool Sim7070AtModem::waitForResponse(const std::string& expected,
                                     std::chrono::milliseconds timeout_ms) {
  // Simplified implementation of waiting for a response
  auto start = std::chrono::high_resolution_clock::now();

  while (true) {
    auto elapsed =
        std::chrono::high_resolution_clock::now() - start;
    if (elapsed > timeout_ms) {
      return false;
    }

    if (auto response = serial_->ReadData()) {
      std::string response_str(response->begin(), response->end());
      if (response_str.find(expected) != std::string::npos) {
        return true;
      }
      if (response_str.find("ERROR") != std::string::npos) {
        return false;
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
}

} /* namespace ae */
