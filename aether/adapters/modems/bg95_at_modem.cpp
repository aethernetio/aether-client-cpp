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
  int err{0};

  if (err >= 0) {
    sendATCommand("AT");  // Checking the connection
    err = CheckResponce("OK", 1000, "AT command error!");
  }
  if (err >= 0) {
    sendATCommand("ATE0");  // Turning off the echo
    err = CheckResponce("OK", 1000, "ATE command error!");
  }
  if (err >= 0) {
    sendATCommand("AT+CMEE=2");  // Enabling extended errors
    err = CheckResponce("OK", 1000, "AT+CMEE command error!");
  }

  if (err < 0) {
    modem_error_event_.Emit(err);
  }
}

void Bg95AtModem::Setup() {
  int err{0};

  // Configuring modem settings
  if (err >= 0) {
    err = SetBaudRate(modem_init_.serial_init.baud_rate);
  }

  float power = 23.5; // DBm
  SetTxPower(kModemBand::kWCDMA_B1, power);
  GetTxPower(kModemBand::kWCDMA_B1, power);

  if (err < 0) {
    modem_error_event_.Emit(err);
  } else {
    modem_connected_event_.Emit(true);
  }
}

void Bg95AtModem::Stop() {
  // int err{0};
}

void Bg95AtModem::OpenNetwork(ae::Protocol protocol, std::string host,
                              std::uint16_t port) {
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Protocol {}", protocol);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Host {}", host);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Port {}", port);
}

void Bg95AtModem::WritePacket(std::vector<uint8_t> const& data) {
  serial_->WriteData(data);
};

void Bg95AtModem::ReadPacket(std::vector<std::uint8_t>& data) {
  auto response = serial_->ReadData();
  std::vector<std::uint8_t> response_vector(response->begin(), response->end());
  data = response_vector;
};

//=============================private members=============================//
int Bg95AtModem::CheckResponce(std::string responce, std::uint32_t wait_time,
                               std::string error_message) {
  int err{0};

  if (!waitForResponse(responce, std::chrono::milliseconds(wait_time))) {
    AE_TELE_ERROR(kAdapterModemAtError, error_message);
    err = -1;
  }

  return err;
}

int Bg95AtModem::SetBaudRate(std::uint32_t rate) {
  int err{0};

  switch (rate) {
    case 9600:
      sendATCommand("AT+IPR=9600");  // Set modem usart speed 9600
      break;
    case 19200:
      sendATCommand("AT+IPR=19200");  // Set modem usart speed 19200
      break;
    case 38400:
      sendATCommand("AT+IPR=38400");  // Set modem usart speed 38400
      break;
    case 57600:
      sendATCommand("AT+IPR=57600");  // Set modem usart speed 57600
      break;
    case 115200:
      sendATCommand("AT+IPR=115200");  // Set modem usart speed 115200
      break;
    case 230400:
      sendATCommand("AT+IPR=230400");  // Set modem usart speed 230400
      break;
    case 460800:
      sendATCommand("AT+IPR=460800");  // Set modem usart speed 460800
      break;
    case 921600:
      sendATCommand("AT+IPR=921600");  // Set modem usart speed 921600
      break;
    case 2900000:
      sendATCommand("AT+IPR=2900000");  // Set modem usart speed 2900000
      break;
    case 3000000:
      sendATCommand("AT+IPR=3000000");  // Set modem usart speed 3000000
      break;
    case 3200000:
      sendATCommand("AT+IPR=3200000");  // Set modem usart speed 3200000
      break;
    case 3684000:
      sendATCommand("AT+IPR=3684000");  // Set modem usart speed 3684000
      break;
    case 4000000:
      sendATCommand("AT+IPR=4000000");  // Set modem usart speed 4000000
      break;
    default:
      err = -2;
      break;
  }

  if (err >= 0) {
    err = CheckResponce("OK", 1000, "No response from modem!");
  }

  return err;
}

int Bg95AtModem::SetTxPower(kModemBand band, const float& power) {
  int err{0};
  std::string power_command;
  
  switch (band) {
    // kWCDMA bands
    case kModemBand::kWCDMA_B1:
      power_command = "AT+QNVW=539,0,\"9B9EA09999A3A5A6\"";
      break;
    case kModemBand::kWCDMA_B2:
      power_command = "AT+QNVW=1183,0,\"9C9EA09999A3A5A6\"";
      break;
    case kModemBand::kWCDMA_B4:
      power_command = "AT+QNVW=4035,0,\"9C9EA09999A3A5A6\"";
      break;
    case kModemBand::kWCDMA_B5:
      power_command = "AT+QNVW=1863,0,\"9C9EA09999A3A5A6\"";
      break;
    case kModemBand::kWCDMA_B8:
      power_command = "AT+QNVW=3690,0,\"9C9EA09999A3A5A6\"";      
      break;
    default:
      err = -2;
      break;
  }
  
  sendATCommand(power_command);
  if (err >= 0) {
    err = CheckResponce("OK", 1000, "No response from modem!");
  }
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Operator name {}", power);
  
  return err;
}

int Bg95AtModem::GetTxPower(kModemBand band, float& power){
  int err{0};

  switch (band) {
    case kModemBand::kWCDMA_B1:
      sendATCommand("AT+QNVR=539,0"); 
      break;
      
    default:
      err = -2;
      break;
  }
  
  auto response = serial_->ReadData();
  
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Power {}", power);
  
  return err;
}

int DbmaToHex(kModemBand band, const float& power, std::string& hex){
  int err{0};
  std::uint8_t byte;
  
  if (kModemBand::kWCDMA_B1 <= band && band <= kModemBand::kWCDMA_B8) {  
    // kWCDMA bands
    // 22 dBm = (22-6.7)*10= 0x99
    byte = (22-6.7)*10;
  } else if (kModemBand::kLTE_B1 <= band && band <= kModemBand::kLTE_B41) {
  
  } else if (kModemBand::kTDS_B34 <= band && band <= kModemBand::kTDS_B39) {

  } else if (kModemBand::kGSM_850 <= band && band <= kModemBand::kGSM_1900) {

  } else if (kModemBand::kTDSCDMA_B34 <= band &&
             band <= kModemBand::kTDSCDMA_B39) {
  } else {
    err = -2;
  }

  AE_TELE_ERROR(kAdapterSerialNotOpen, "Band {}", band);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Power {}", power);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Hex {}", hex);
  
  return err;
}

int HexToDbma(kModemBand band, float& power, const std::string& hex){
  int err{0};
  
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Band {}", band);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Power {}", power);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Hex {}", hex);
  
  return err;
}
  
void Bg95AtModem::sendATCommand(const std::string& command) {
  std::vector<uint8_t> data(command.begin(), command.end());
  data.push_back('\r');  // Adding a carriage return symbols
  data.push_back('\n');
  serial_->WriteData(data);
}

bool Bg95AtModem::waitForResponse(const std::string& expected,
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
