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

Sim7070AtModem::Sim7070AtModem(ModemInit modem_init) : modem_init_(modem_init) {
  serial_ = SerialPortFactory::CreatePort(modem_init_.serial_init);
};

void Sim7070AtModem::Init() {
  int err{0};

  sendATCommand("AT");  // Checking the connection
  if (!waitForResponse("OK", std::chrono::milliseconds(1000))) {
    AE_TELE_ERROR(kAdapterSerialNotOpen, "No response from modem");
    err = -1;
  }

  sendATCommand("ATE0");       // Turning off the echo
  sendATCommand("AT+CMEE=2");  // Enabling extended errors

  if (err < 0) {
    modem_error_event_.Emit(err);
  }
}

void Sim7070AtModem::Setup() {
  int err{0};

  // Configuring modem settings
  sendATCommand("AT+CFUN=1");  // Enabling full functionality
  sendATCommand("AT+CNMP=2");  // Preferred network mode: LTE
  sendATCommand("AT+CMNB=1");  // Preferred mode: CAT-M

  err = SetBaudRate(modem_init_.serial_init.baud_rate);

  if (err < 0) {
    modem_error_event_.Emit(err);
  } else {
    modem_connected_event_.Emit(true);
  }
}

void Sim7070AtModem::Stop() {
  int err{0};

  sendATCommand("ATZ");  // Turning off the modem correctly

  if (err < 0) {
    modem_error_event_.Emit(err);
  } else {
    modem_connected_event_.Emit(false);
  }
}

void Sim7070AtModem::WritePacket(std::vector<uint8_t> const& data) {

};

void Sim7070AtModem::ReadPacket(std::vector<std::uint8_t>& data) {

};

void Sim7070AtModem::PowerOff() {
  int err{0};

  sendATCommand("AT+CPOWD=1");  // Set modem power OFF

  if (err < 0) {
    modem_error_event_.Emit(err);
  } else {
    modem_connected_event_.Emit(false);
  }
};

//===============================private members============================
int Sim7070AtModem::SetBaudRate(std::uint32_t rate) {
  int err{0};

  switch (rate) {
    case 0:
      sendATCommand("AT+IPR=0");  // Set modem usart speed
      break;
    case 300:
      sendATCommand("AT+IPR=300");  // Set modem usart speed
      break;
    case 600:
      sendATCommand("AT+IPR=600");  // Set modem usart speed
      break;
    case 1200:
      sendATCommand("AT+IPR=1200");  // Set modem usart speed
      break;
    case 2400:
      sendATCommand("AT+IPR=2400");  // Set modem usart speed
      break;
    case 4800:
      sendATCommand("AT+IPR=4800");  // Set modem usart speed
      break;
    case 9600:
      sendATCommand("AT+IPR=9600");  // Set modem usart speed
      break;
    case 19200:
      sendATCommand("AT+IPR=19200");  // Set modem usart speed
      break;
    case 38400:
      sendATCommand("AT+IPR=38400");  // Set modem usart speed
      break;
    case 57600:
      sendATCommand("AT+IPR=57600");  // Set modem usart speed
      break;
    case 115200:
      sendATCommand("AT+IPR=115200");  // Set modem usart speed
      break;
    case 230400:
      sendATCommand("AT+IPR=230400");  // Set modem usart speed
      break;
    case 921600:
      sendATCommand("AT+IPR=921600");  // Set modem usart speed
      break;
    case 2000000:
      sendATCommand("AT+IPR=2000000");  // Set modem usart speed
      break;
    case 2900000:
      sendATCommand("AT+IPR=2900000");  // Set modem usart speed
      break;
    case 3000000:
      sendATCommand("AT+IPR=3000000");  // Set modem usart speed
      break;
    case 3200000:
      sendATCommand("AT+IPR=3200000");  // Set modem usart speed
      break;
    case 3684000:
      sendATCommand("AT+IPR=3684000");  // Set modem usart speed
      break;
    case 4000000:
      sendATCommand("AT+IPR=4000000");  // Set modem usart speed
      break;
    default:

      break;
  }

  if (!waitForResponse("OK", std::chrono::milliseconds(1000))) {
    AE_TELE_ERROR(kAdapterSerialNotOpen, "No response from modem");
    err = -1;
  }

  return err;
}

int Sim7070AtModem::SetupSim(std::uint8_t pin[4]) {}

int Sim7070AtModem::SetupNetwork(std::string operator_name,
                                 std::string apn_name, std::string apn_user,
                                 std::string apn_pass) {}

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
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
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
