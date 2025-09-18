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

#include "aether/lora_modules/dx_smart_lr02_433_lm.h"
#include "aether/serial_ports/serial_port_factory.h"

#include "aether/lora_modules/lora_modules_tele.h"

namespace ae {

DxSmartLr02LoraModule::DxSmartLr02LoraModule(LoraModuleInit lora_module_init,
                                             Domain* domain)
    : ILoraModuleDriver{std::move(lora_module_init), domain} {
  serial_ = SerialPortFactory::CreatePort(GetLoraModuleInit().serial_init);
  at_comm_support_ = std::make_unique<AtCommSupport>(serial_.get());
};

bool DxSmartLr02LoraModule::Init() {
  LoraModuleInit lora_module_init = GetLoraModuleInit();
  kLoraModuleError err{kLoraModuleError::kNoError};
  
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open.");
    return false;
  }

  err = EnterAtMode();

  if (err != kLoraModuleError::kNoError) {
    AE_TELED_ERROR("Can't enter to the AT command mode.");
    return false;
  }

  at_comm_support_->SendATCommand("AT");  // Checking the connection
  if (err = CheckResponse("OK", 1000, "AT command error!");
      err != kLoraModuleError::kNoError) {
    AE_TELED_ERROR("AT command error {}", err);
    return false;
  }
  
  if (err = SetupSerialPort(lora_module_init.serial_init);
      err != kLoraModuleError::kNoError) {
    AE_TELED_ERROR("Setup module serial port error {}", err);
    return false;
  }
  
  err = ExitAtMode();

  if (err != kLoraModuleError::kNoError) {
    AE_TELED_ERROR("Can't exit from the AT command mode.");
    return false;
  }

  return true;
};

bool DxSmartLr02LoraModule::Start() {
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open");
    return false;
  }

  return true;
};

bool DxSmartLr02LoraModule::Stop() {
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open");
    return false;
  }

  return true;
};

// =============================private members=========================== //
kLoraModuleError DxSmartLr02LoraModule::CheckResponse(
    std::string const& response, std::uint32_t const wait_time,
    std::string const& error_message) {
  kLoraModuleError err{kLoraModuleError::kNoError};

  if (!at_comm_support_->WaitForResponse(
          response, std::chrono::milliseconds(wait_time))) {
    AE_TELED_ERROR(error_message);
    err = kLoraModuleError::kAtCommandError;
  }

  return err;
}

kLoraModuleError DxSmartLr02LoraModule::EnterAtMode() {
  kLoraModuleError err{kLoraModuleError::kNoError};
  
  if (at_mode_ == false) {
    at_comm_support_->SendATCommand("+++");
    if (err =
            CheckResponse("Entry AT", 1000, "EnterAtMode command error!");
        err != kLoraModuleError::kNoError) {          
      AE_TELED_ERROR("AT command error {}", err);
    } else {
      at_mode_ = true;
    }
  }
  
  return err;
};

kLoraModuleError DxSmartLr02LoraModule::ExitAtMode() {
  kLoraModuleError err{kLoraModuleError::kNoError};
  
  if (at_mode_ == true) {
    at_comm_support_->SendATCommand("+++");
    if (err =
            CheckResponse("Exit AT", 1000, "ExitAtMode command error!");
        err != kLoraModuleError::kNoError) {
      AE_TELED_ERROR("AT command error {}", err);
    } else {
      at_mode_ = false;
    }
  }
  
  return err;
};

kLoraModuleError DxSmartLr02LoraModule::SetupSerialPort(SerialInit serial_init){
  kLoraModuleError err{kLoraModuleError::kNoError};
  
  err = SetBaudRate(serial_init.baud_rate);
  
  if (err != kLoraModuleError::kNoError) {
    return err;
  }
  
  err = SetParity(serial_init.parity);
  
  if (err != kLoraModuleError::kNoError) {
    return err;
  }
  
  err = SetStopBits(serial_init.stop_bits);

  return err;
}

kLoraModuleError DxSmartLr02LoraModule::SetBaudRate(kBaudRate baud_rate){
  kLoraModuleError err{kLoraModuleError::kNoError};
  
  auto it = baud_rate_commands_lr02.find(baud_rate);
  if (it == baud_rate_commands_lr02.end()) {
    err = kLoraModuleError::kBaudRateError;
    return err;
  }

  at_comm_support_->SendATCommand(it->second);
  err = CheckResponse("OK", 1000, "No response from lora module!");
  if (err != kLoraModuleError::kNoError) {
    err = kLoraModuleError::kBaudRateError;
  }
  
  return err;
}

kLoraModuleError DxSmartLr02LoraModule::SetParity(kParity parity){
  switch (parity) {
    case kParity::kNoParity:
      at_comm_support_->SendATCommand("AT+PARI0");  // Set no parity
      break;
    case kParity::kOddParity:
      at_comm_support_->SendATCommand("AT+PARI1");  // Set odd parity
      break;
    case kParity::kEvenParity:
      at_comm_support_->SendATCommand("AT+PARI2");  // Set even parity
      break;
    default:
      return kLoraModuleError::kParityError;
      break;
  }

  auto err = CheckResponse("OK", 1000, "No response from lora module!");
  if (err != kLoraModuleError::kNoError) {
    return kLoraModuleError::kParityError;
  }

  return kLoraModuleError::kNoError;
}

kLoraModuleError DxSmartLr02LoraModule::SetStopBits(kStopBits stop_bits){
    switch (stop_bits) {
    case kStopBits::kOneStopBit:
      at_comm_support_->SendATCommand("AT+STOP0");  // 0 stop bits
      break;
    case kStopBits::kTwoStopBit:
      at_comm_support_->SendATCommand("AT+PARI1");  // 2 stop bits
      break;
    default:
      return kLoraModuleError::kStopBitsError;
      break;
  }

  auto err = CheckResponse("OK", 1000, "No response from lora module!");
  if (err != kLoraModuleError::kNoError) {
    return kLoraModuleError::kStopBitsError;
  }

  return kLoraModuleError::kNoError;
}
  
kLoraModuleError DxSmartLr02LoraModule::SetupLoraNet(LoraModuleInit lora_module_init){
  kLoraModuleError err{kLoraModuleError::kNoError};
  
/*lora_module_my_adress, 
lora_module_bs_adress,
lora_module_channel, 
lora_module_mode, 
lora_module_level,
lora_module_power, 
lora_module_band_width,
lora_module_coding_rate, 
lora_module_spreading_factor,
lora_module_crc_check, 
lora_module_signal_inversion*/

  return err;
}

}  // namespace ae
