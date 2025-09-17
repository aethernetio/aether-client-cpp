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

DxSmartLr02LoraModule::DxSmartLr02LoraModule(LoraModuleInit lora_module_init, Domain* domain)
    : ILoraModuleDriver{std::move(lora_module_init), domain} {
  serial_ = SerialPortFactory::CreatePort(GetLoraModuleInit().serial_init);
  at_comm_support_ = std::make_unique<AtCommSupport>(serial_.get());
};

bool DxSmartLr02LoraModule::Init() {
  if (!serial_->IsOpen()) {
    AE_TELED_ERROR("Serial port is not open");
    return false;
  }
  
  EnterAtMode();
  
  at_comm_support_->SendATCommand("AT");  // Checking the connection
  if (auto err = CheckResponse("OK", 1000, "AT command error!");
      err != kLoraModuleError::kNoError) {
    AE_TELED_ERROR("AT command error {}", err);
    return false;
  }
  
  return true;
};

bool DxSmartLr02LoraModule::Start() {
  LoraModuleInit lora_module_init = GetLoraModuleInit();

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
kLoraModuleError DxSmartLr02LoraModule::CheckResponse(std::string const& response,
                                          std::uint32_t const wait_time,
                                          std::string const& error_message) {
  kLoraModuleError err{kLoraModuleError::kNoError};

  if (!at_comm_support_->WaitForResponse(
          response, std::chrono::milliseconds(wait_time))) {
    AE_TELED_ERROR(error_message);
    err = kLoraModuleError::kAtCommandError;
  }

  return err;
}

void DxSmartLr02LoraModule::EnterAtMode(){
  if(at_mode_ == false){
  at_comm_support_->SendATCommand("+++");
  at_mode_ = true;
  }
};

void DxSmartLr02LoraModule::LeaveAtMode(){
  if(at_mode_ == true){
  at_comm_support_->SendATCommand("+++");
  at_mode_ = false;
  }
};

}  // namespace ae
