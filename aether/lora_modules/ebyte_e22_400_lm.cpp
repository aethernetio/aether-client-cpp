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

#include "aether/lora_modules/ebyte_e22_400_lm.h"
#include "aether/serial_ports/serial_port_factory.h"

#include "aether/lora_modules/lora_modules_tele.h"

namespace ae {

EbyteE22LoraModule::EbyteE22LoraModule(LoraModuleAdapter& adapter,
                                       IPoller::ptr poller,
                                       LoraModuleInit lora_module_init,
                                       Domain* domain)
    : ILoraModuleDriver{std::move(poller), std::move(lora_module_init), domain},
      adapter_{&adapter} {
  serial_ = SerialPortFactory::CreatePort(*adapter_->aether_.as<Aether>(),
                                          adapter_->poller_,
                                          GetLoraModuleInit().serial_init);
};

bool EbyteE22LoraModule::Init() { return true; };

bool EbyteE22LoraModule::Start() { return true; };

bool EbyteE22LoraModule::Stop() { return true; };

ConnectionLoraIndex EbyteE22LoraModule::OpenNetwork(ae::Protocol /* protocol*/, std::string const& /*host*/,
                                                    std::uint16_t /* port*/) { 
                                
  ConnectionLoraIndex index{-1};
  
  return index; };

void EbyteE22LoraModule::CloseNetwork(
    ae::ConnectionLoraIndex /*connect_index*/) {};

void EbyteE22LoraModule::WritePacket(ae::ConnectionLoraIndex /*connect_index*/,
                 ae::DataBuffer const& /*data*/) {};

DataBuffer EbyteE22LoraModule::ReadPacket(
    ae::ConnectionLoraIndex /*connect_index*/,
                      ae::Duration /*timeout*/) { 
  DataBuffer data{};
  
  return data; };

bool EbyteE22LoraModule::SetPowerSaveParam(std::string const& /*psp*/) {
  return true;
};

bool EbyteE22LoraModule::PowerOff() { return true; };

bool EbyteE22LoraModule::SetLoraModuleAddress(
    std::uint16_t const& /*address*/) {
  return true;
};

bool EbyteE22LoraModule::SetLoraModuleChannel(std::uint8_t const& /*channel*/)
{
  return true;
};

bool EbyteE22LoraModule::SetLoraModuleMode(kLoraModuleMode const& /*mode*/)
{
  return true;
};

bool EbyteE22LoraModule::SetLoraModuleLevel(kLoraModuleLevel const& /*level*/)
{
  return true;
};

bool EbyteE22LoraModule::SetLoraModulePower(kLoraModulePower const& /*power*/)
{
  return true;
};

bool EbyteE22LoraModule::SetLoraModuleBandWidth(
    kLoraModuleBandWidth const& /*band_width*/)
{
  return true;
};

bool EbyteE22LoraModule::SetLoraModuleCodingRate(
    kLoraModuleCodingRate const& /*coding_rate*/)
{
  return true;
};

bool EbyteE22LoraModule::SetLoraModuleSpreadingFactor(
    kLoraModuleSpreadingFactor const& /*spreading_factor*/)
{
  return true;
};

bool EbyteE22LoraModule::SetLoraModuleCRCCheck(
    kLoraModuleCRCCheck const& /*crc_check*/)
{
  return true;
};

bool EbyteE22LoraModule::SetLoraModuleIQSignalInversion(
    kLoraModuleIQSignalInversion const& /*signal_inversion*/)
{
  return true;
};

}  // namespace ae
