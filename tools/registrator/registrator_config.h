/*
 * Copyright 2024 Aethernet Inc.
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

#ifndef TOOLS_REGISTRATOR_REGISTRATOR_CONFIG_H_
#define TOOLS_REGISTRATOR_REGISTRATOR_CONFIG_H_

#include <string>
#include <vector>

#include "aether/address.h"

namespace ae {
enum class ServerAddressType : std::uint8_t { kIpAddress, kUrlAddress };

struct ServerConfig {
  ae::ServerAddressType server_address_type;
  ae::IpAddress::Version server_ip_address_version;
  std::string server_address;
  std::uint16_t server_port;
  ae::Protocol server_protocol;
  ae::IpAddress server_ip_adress;
};

struct ClientParents {
  std::string uid_str;
  std::uint8_t clients_num;
};

class RegistratorConfig {
 public:
  RegistratorConfig(const std::string& ini_file);

  int ParseConfig();
  std::vector<ae::ClientParents> GetParents();
  std::vector<ae::ServerConfig> GetServers();
  std::uint8_t GetClientsTotal();
  std::string GetWiFiSsid();
  std::string GetWiFiPass();
  bool GetWiFiIsSet();

 private:
  const std::string ini_file_;
  std::vector<ae::ClientParents> parents_;
  std::vector<ae::ServerConfig> servers_;
  std::uint8_t clients_total_{0};
  std::string wifi_ssid_{};
  std::string wifi_pass_{};
};
}  // namespace ae

#endif  // TOOLS_REGISTRATOR_REGISTRATOR_CONFIG_H_