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
  kModemError err{kModemError::kNoError};

  if (err == kModemError::kNoError) {
    sendATCommand("AT");  // Checking the connection
    err = CheckResponce("OK", 1000, "AT command error!");
  }
  if (err == kModemError::kNoError) {
    sendATCommand("ATE0");  // Turning off the echo
    err = CheckResponce("OK", 1000, "ATE command error!");
  }
  if (err == kModemError::kNoError) {
    sendATCommand("AT+CMEE=2");  // Enabling extended errors
    err = CheckResponce("OK", 1000, "AT+CMEE command error!");
  }

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  }
}

void Bg95AtModem::Start() {
  kModemError err{kModemError::kNoError};

  // Configuring modem settings
  // Serial port speed
  if (err == kModemError::kNoError) {
    err = SetBaudRate(modem_init_.serial_init.baud_rate);
  }

  // Enabling full functionality
  if (err == kModemError::kNoError) {
    sendATCommand("AT+CFUN=1");
    err = CheckResponce("OK", 1000, "AT+CFUN command error!");
  }

  if (err == kModemError::kNoError) {
    err = CheckSimStatus();
  }

  if (err == kModemError::kNoError && modem_init_.use_pin == true) {
    err = SetupSim(modem_init_.pin);
  }

  if (err == kModemError::kNoError) {
    err = SetupNetwork(modem_init_.operator_name, modem_init_.operator_code,
                       modem_init_.apn_name, modem_init_.apn_user,
                       modem_init_.apn_pass);
  }

  // float power = 23.5;  // DBm
  // err = SetTxPower(kModemBand::kWCDMA_B1, power);
  // err = GetTxPower(kModemBand::kWCDMA_B1, power);

  if (err != kModemError::kNoError) {
    modem_error_event_.Emit(static_cast<int>(err));
  } else {
    modem_connected_event_.Emit(true);
  }
}

void Bg95AtModem::Stop() {}

void Bg95AtModem::OpenNetwork(std::uint8_t const context_index,
                              std::uint8_t const connect_index,
                              ae::Protocol const protocol,
                              std::string const host,
                              std::uint16_t const port) {
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Context index {}", context_index);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Connect index {}", connect_index);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Protocol {}", protocol);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Host {}", host);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Port {}", port);
}

void Bg95AtModem::CloseNetwork(std::uint8_t const context_index,
                               std::uint8_t const connect_index) {
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Context index {}", context_index);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Connect index {}", connect_index);
}

void Bg95AtModem::WritePacket(std::uint8_t const connect_index,
                              std::vector<uint8_t> const& data) {
  serial_->WriteData(data);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Connect index {}", connect_index);
};

void Bg95AtModem::ReadPacket(std::uint8_t const connect_index,
                             std::vector<std::uint8_t>& data,
                             std::int32_t timeout) {
  //std::size_t size{};

      auto response = serial_->ReadData();
  std::vector<std::uint8_t> response_vector(response->begin(), response->end());
  data = response_vector;
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Connect index {}", connect_index);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Timeout {}", timeout);
};

//=============================private members=============================//
kModemError Bg95AtModem::CheckResponce(std::string responce,
                                       std::uint32_t wait_time,
                                       std::string error_message) {
  kModemError err{kModemError::kNoError};

  if (!waitForResponse(responce, std::chrono::milliseconds(wait_time))) {
    AE_TELE_ERROR(kAdapterModemAtError, error_message);
    err = kModemError::kAtCommandError;
  }

  return err;
}

kModemError Bg95AtModem::SetBaudRate(std::uint32_t rate) {
  kModemError err{kModemError::kNoError};

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
      err = kModemError::kBaudRateError;
      return err;
      break;
  }

  err = CheckResponce("OK", 1000, "No response from modem!");
  if (err != kModemError::kNoError) {
    err = kModemError::kBaudRateError;
  }

  return err;
}

kModemError Bg95AtModem::CheckSimStatus() {
  kModemError err{kModemError::kNoError};

  sendATCommand("AT+CPIN?");  // Check SIM card status
  err = CheckResponce("OK", 1000, "SIM card error!");
  if (err != kModemError::kNoError) {
    err = kModemError::kCheckSimStatus;
  }

  return err;
}

kModemError Bg95AtModem::SetupSim(const std::uint8_t pin[4]) {
  kModemError err{kModemError::kNoError};

  auto pin_string = pinToString(pin);

  if (pin_string == "ERROR") {
    err = kModemError::kPinWrong;
    return err;
  }

  sendATCommand("AT+CPIN=" + pin_string);  // Check SIM card status
  err = CheckResponce("OK", 1000, "SIM card PIN error!");
  if (err != kModemError::kNoError) {
    err = kModemError::kSetupSim;
  }

  return err;
}

kModemError Bg95AtModem::SetTxPower(kModemBand band, const float& power) {
  kModemError err{kModemError::kNoError};
  std::string power_command, hex;

  err = DbmaToHex(band, power, hex);

  if (err == kModemError::kNoError) {
    switch (band) {
      // kWCDMA bands
      case kModemBand::kWCDMA_B1:
        power_command = "AT+QNVW=539,0,\"9B9EA0" + hex + "A3A5A6\"";
        break;
      case kModemBand::kWCDMA_B2:
        power_command = "AT+QNVW=1183,0,\"9C9EA0" + hex + "A3A5A6\"";
        break;
      case kModemBand::kWCDMA_B4:
        power_command = "AT+QNVW=4035,0,\"9C9EA0" + hex + "A3A5A6\"";
        break;
      case kModemBand::kWCDMA_B5:
        power_command = "AT+QNVW=1863,0,\"9C9EA0" + hex + "A3A5A6\"";
        break;
      case kModemBand::kWCDMA_B8:
        power_command = "AT+QNVW=3690,0,\"9C9EA0" + hex + "A3A5A6\"";
        break;
      // kLTE bands
      case kModemBand::kLTE_B1:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00020992\",0100" + hex + "00";
        break;
      case kModemBand::kLTE_B2:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00020993\",0100" + hex + "00";
        break;
      case kModemBand::kLTE_B3:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00020994\",0100" + hex + "00";
        break;
      case kModemBand::kLTE_B4:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00020995\",0100" + hex + "00";
        break;
      case kModemBand::kLTE_B5:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00020996\",0100" + hex + "00";
        break;
      case kModemBand::kLTE_B7:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00020997\",0100" + hex + "00";
        break;
      case kModemBand::kLTE_B8:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00020998\",0100" + hex + "00";
        break;
      case kModemBand::kLTE_B12:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00022191\",0100" + hex + "00";
        break;
      case kModemBand::kLTE_B13:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00021000\",0100" + hex + "00";
        break;
      case kModemBand::kLTE_B17:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00021001\",0100" + hex + "00";
        break;
      case kModemBand::kLTE_B18:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00021002\",0100" + hex + "00";
        break;
      case kModemBand::kLTE_B19:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00023133\",0100" + hex + "00";
        break;
      case kModemBand::kLTE_B20:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00021003\",0100" + hex + "00";
        break;
      case kModemBand::kLTE_B25:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00022351\",0100" + hex + "00";
        break;
      case kModemBand::kLTE_B26:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00024674\",0100" + hex + "00";
        break;
      case kModemBand::kLTE_B28:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00025477\",0100" + hex + "00";
        break;
      case kModemBand::kLTE_B38:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00021004\",0100" + hex + "00";
        break;
      case kModemBand::kLTE_B39:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00023620\",0100" + hex + "00";
        break;
      case kModemBand::kLTE_B40:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00021005\",0100" + hex + "00";
        break;
      case kModemBand::kLTE_B41:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00021670\",0100" + hex + "00";
        break;
      case kModemBand::kTDS_B34:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00022622\",0100" + hex + "00";
        break;
      case kModemBand::kTDS_B39:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/00022663\",0100" + hex + "00";
        break;
      case kModemBand::kGSM_850:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/"
            "00025076\","
            "C2018A0252031A04E204AA0572063A070208CA0892095A0A220BEA0B" +
            hex + hex;
        break;
      case kModemBand::kGSM_900:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/"
            "00025077\","
            "C2018A0252031A04E204AA0572063A070208CA0892095A0A220BEA0B" +
            hex + hex;
        break;
      case kModemBand::kGSM_1800:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/"
            "00025078\","
            "000096005E012602EE02B6037E0446050E06D6069E0766082E09F609BE0A" +
            hex;
        break;
      case kModemBand::kGSM_1900:
        power_command =
            "AT+QNVFW=\"/nv/item_files/rfnv/"
            "00025079\","
            "000096005E012602EE02B6037E0446050E06D6069E0766082E09F609BE0A" +
            hex;
        break;
      case kModemBand::kTDSCDMA_B34:
        power_command = "AT+QNVFW=\"/nv/item_files/rfnv/00022622\"," + hex;
        break;
      case kModemBand::kTDSCDMA_B39:
        power_command = "AT+QNVFW=\"/nv/item_files/rfnv/00022663\"," + hex;
        break;
      default:
        err = kModemError::kSetTxPowerBand;
        return err;
        break;
    }
  }

  sendATCommand(power_command);
  err = CheckResponce("OK", 1000, "No response from modem!");
  if (err != kModemError::kNoError) {
    err = kModemError::kSetTxPowerBand;
  }

  return err;
}

kModemError Bg95AtModem::GetTxPower(kModemBand band, float& power) {
  kModemError err{kModemError::kNoError};
  std::string hex;

  switch (band) {
      // kWCDMA bands
    case kModemBand::kWCDMA_B1:
      sendATCommand("AT+QNVR=539,0");
      break;
    case kModemBand::kWCDMA_B2:
      sendATCommand("AT+QNVR=1183,0");
      break;
    case kModemBand::kWCDMA_B4:
      sendATCommand("AT+QNVR=4035,0");
      break;
    case kModemBand::kWCDMA_B5:
      sendATCommand("AT+QNVR=1863,0");
      break;
    case kModemBand::kWCDMA_B8:
      sendATCommand("AT+QNVR=3690,0");
      break;
    // kLTE bands
    case kModemBand::kLTE_B1:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00020992\",0100");
      break;
    case kModemBand::kLTE_B2:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00020993\",0100");
      break;
    case kModemBand::kLTE_B3:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00020994\",0100");
      break;
    case kModemBand::kLTE_B4:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00020995\",0100");
      break;
    case kModemBand::kLTE_B5:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00020996\",0100");
      break;
    case kModemBand::kLTE_B7:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00020997\",0100");
      break;
    case kModemBand::kLTE_B8:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00020998\",0100");
      break;
    case kModemBand::kLTE_B12:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00022191\",0100");
      break;
    case kModemBand::kLTE_B13:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00021000\",0100");
      break;
    case kModemBand::kLTE_B17:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00021001\",0100");
      break;
    case kModemBand::kLTE_B18:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00021002\",0100");
      break;
    case kModemBand::kLTE_B19:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00023133\",0100");
      break;
    case kModemBand::kLTE_B20:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00021003\",0100");
      break;
    case kModemBand::kLTE_B25:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00022351\",0100");
      break;
    case kModemBand::kLTE_B26:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00024674\",0100");
      break;
    case kModemBand::kLTE_B28:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00025477\",0100");
      break;
    case kModemBand::kLTE_B38:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00021004\",0100");
      break;
    case kModemBand::kLTE_B39:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00023620\",0100");
      break;
    case kModemBand::kLTE_B40:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00021005\",0100");
      break;
    case kModemBand::kLTE_B41:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00021670\",0100");
      break;
    case kModemBand::kTDS_B34:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00022622\",0100");
      break;
    case kModemBand::kTDS_B39:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00022663\",0100");
      break;
    case kModemBand::kGSM_850:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00025076\"");
      break;
    case kModemBand::kGSM_900:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00025077\"");
      break;
    case kModemBand::kGSM_1800:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00025078\"");
      break;
    case kModemBand::kGSM_1900:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00025079\"");
      break;
    case kModemBand::kTDSCDMA_B34:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00022622\"");
      break;
    case kModemBand::kTDSCDMA_B39:
      sendATCommand("AT+QNVFR=\"/nv/item_files/rfnv/00022663\"");
      break;
    default:
      err = kModemError::kGetTxPowerBand;
      return err;
      break;
  }

  auto response = serial_->ReadData();

  std::string response_str(response->begin(), response->end());

  hex = response_str;

  HexToDbma(band, power, hex);

  return err;
}

kModemError Bg95AtModem::DbmaToHex(kModemBand band, const float& power,
                                   std::string& hex) {
  kModemError err{kModemError::kNoError};
  std::uint8_t byte1, byte2;
  std::uint16_t word;
  std::stringstream ss;

  if (kModemBand::kWCDMA_B1 <= band && band <= kModemBand::kWCDMA_B8) {
    // kWCDMA bands
    // 22 dBm = (22-6.7)*10= 0x99
    if (power >= 21 && power <= 24.5) {
      byte1 = static_cast<std::uint8_t>((power - 6.7f) * 10);
      ss << std::hex << std::setw(2) << static_cast<int>(byte1)
         << static_cast<int>(byte1);
      hex = ss.str();
    } else {
      err = kModemError::kDbmaToHexRange;
    }
  } else if (kModemBand::kLTE_B1 <= band && band <= kModemBand::kLTE_B41) {
    // kLTE bands
    // 24 dBm = 24*10 = 240 ----> F0
    if (power >= 21 && power <= 24.5) {
      byte1 = static_cast<std::uint8_t>(power * 10);
      ss << std::hex << std::setw(2) << static_cast<int>(byte1);
      hex = ss.str();
    } else {
      err = kModemError::kDbmaToHexRange;
    }
  } else if (kModemBand::kTDS_B34 <= band && band <= kModemBand::kTDS_B39) {
    // kTDS bands
    // 24 dBm = 24*10 = 240 ----> F0
    if (power >= 21 && power <= 24.5) {
      byte1 = static_cast<std::uint8_t>(power * 10);
      ss << std::hex << std::setw(2) << static_cast<int>(byte1);
      hex = ss.str();
    } else {
      err = kModemError::kDbmaToHexRange;
    }
  } else if (kModemBand::kGSM_850 <= band && band <= kModemBand::kGSM_1900) {
    // kGSM bands
    // 32 dBm = 32*100= 3200 = 0x0C80(HEX) = 800C
    if (power >= 26 && power <= 33) {
      word = static_cast<std::uint16_t>(power * 100);
      byte1 = static_cast<std::uint8_t>((word >> 8) & 0xFF);
      byte2 = static_cast<std::uint8_t>((word >> 0) & 0xFF);
      ss << std::hex << std::setw(2) << static_cast<int>(byte1)
         << static_cast<int>(byte2);
      hex = ss.str();
    } else {
      err = kModemError::kDbmaToHexRange;
    }
  } else if (kModemBand::kTDSCDMA_B34 <= band &&
             band <= kModemBand::kTDSCDMA_B39) {
    // kTDSCDMA bands
    // 22 dBm = 22*10+700 = 920 = 0x0398(HEX) = 9803
    if (power >= 21 && power <= 24.5) {
      word = static_cast<std::uint16_t>(power * 10 + 700);
      byte1 = static_cast<std::uint8_t>((word >> 8) & 0xFF);
      byte2 = static_cast<std::uint8_t>((word >> 0) & 0xFF);
      ss << std::hex << std::setw(2) << static_cast<int>(byte2)
         << static_cast<int>(byte1);
      hex = ss.str();
    } else {
      err = kModemError::kDbmaToHexRange;
    }
  } else {
    err = kModemError::kDbmaToHexBand;
  }

  return err;
}

kModemError Bg95AtModem::HexToDbma(kModemBand band, float& power,
                                   const std::string& hex) {
  kModemError err{kModemError::kNoError};

  AE_TELE_ERROR(kAdapterSerialNotOpen, "Band {}", band);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Power {}", power);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Hex {}", hex);

  return err;
}

kModemError Bg95AtModem::SetupNetwork(std::string operator_name,
                                      std::string operator_code,
                                      std::string apn_name,
                                      std::string apn_user,
                                      std::string apn_pass) {
  kModemError err{kModemError::kNoError};

  sendATCommand("AT+COPS=1,2,\"" + operator_code + "\",0");
  // err = CheckResponce("OK", 1000, "No response from modem!");
  // if (err != kModemError::kNoError) {}

  AE_TELE_ERROR(kAdapterSerialNotOpen, "Operator name {}", operator_name);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "Operator code {}", operator_code);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "APN name {}", apn_name);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "APN user {}", apn_user);
  AE_TELE_ERROR(kAdapterSerialNotOpen, "APN pass {}", apn_pass);

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
