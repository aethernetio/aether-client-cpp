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

#include "tools/registrator/registrator_config.h"
#include "third_party/ini.h/ini.h"

#include "aether/address_parser.h"
#include "aether/tele/tele.h"

constexpr std::uint8_t clients_max = 4;
constexpr std::uint8_t servers_max = 4;

namespace ae {

/**
 * \brief Class constructor.
 */
RegistratorConfig::RegistratorConfig(const std::string& ini_file)
    : ini_file_{ini_file} {};

/**
 * \brief Config parser.
 */
int RegistratorConfig::ParseConfig() {
  std::string uid;
  std::uint8_t clients_num, clients_total{0};
  std::tuple<std::string, std::uint8_t> parent;

  // Reading settings from the ini file.
  ini::File file = ini::open(ini_file_);

  std::string wifi_ssid = file["Aether"]["wifiSsid"];
  std::string wifi_pass = file["Aether"]["wifiPass"];

  AE_TELED_DEBUG("WiFi ssid={}", wifi_ssid);
  AE_TELED_DEBUG("WiFi pass={}", wifi_pass);

  std::string sodium_key = file["Aether"]["sodiumKey"];
  std::string hydrogen_key = file["Aether"]["hydrogenKey"];

  AE_TELED_DEBUG("Sodium key={}", sodium_key);
  AE_TELED_DEBUG("Hydrogen key={}", hydrogen_key);

  // Clients configuration
  std::int8_t parents_num = file["Aether"].get<int>("parentsNum");

  for (std::uint8_t i{0}; i < parents_num; i++) {
    uid = file["ParentID" + std::to_string(i + 1)]["uid"];
    clients_num =
        file["ParentID" + std::to_string(i + 1)].get<int>("clientsNum");
    ae::ClientParents parent{uid, clients_num};
    parents_.push_back(parent);
    clients_total += clients_num;
  }

  // Clients max assertion
  if (clients_total > clients_max) {
    AE_TELED_ERROR("Total clients must be < {} clients", clients_max);
    return -1;
  }

  clients_total_ = clients_total;

  // Servers configuration
  std::int8_t servers_num = file["Aether"].get<int>("serversNum");

  std::uint8_t servers_total{0};
  ae::ServerConfig server_config{};

  for (std::uint8_t i{0}; i < servers_num; i++) {
    std::string str_ip_address_type =
        file["ServerID" + std::to_string(i + 1)]["ipAddressType"];
    std::string str_ip_address =
        file["ServerID" + std::to_string(i + 1)]["ipAddress"];
    std::uint16_t int_ip_port =
        file["ServerID" + std::to_string(i + 1)].get<int>("ipPort");
    std::string str_ip_protocol =
        file["ServerID" + std::to_string(i + 1)]["ipProtocol"];

    if (str_ip_address_type == "kIpAddress") {
      ae::IpAddress ip_adress{};
      ae::IpAddressParser ip_adress_parser{};

      auto res = ip_adress_parser.StringToIP(str_ip_address);
      if (res == std::nullopt) {
        AE_TELED_ERROR("Configuration failed, wrong IP address {}.",
                       str_ip_address);
        return -2;
      } else {
        ip_adress = *res;
      }

      server_config.server_ip_adress = ip_adress;
      server_config.server_address_type = ae::ServerAddressType::kIpAddress;
      server_config.server_ip_address_version = ip_adress.version;
      server_config.server_address = str_ip_address;
      server_config.server_port = int_ip_port;
      if (str_ip_protocol == "kTcp") {
        server_config.server_protocol = ae::Protocol::kTcp;
      }
    } else if (str_ip_address_type == "kUrlAddress") {
      server_config.server_address_type = ae::ServerAddressType::kUrlAddress;
      server_config.server_address = str_ip_address;
      server_config.server_port = int_ip_port;
      if (str_ip_protocol == "kTcp") {
        server_config.server_protocol = ae::Protocol::kTcp;
      }
    }

    servers_.push_back(server_config);
    servers_total++;
  }

  // Servers max assertion
  if (servers_total > servers_max) {
    AE_TELED_ERROR("Total servers must be < {} servers", servers_max);
    return -3;
  }

  return 0;
}

/**
 * \brief Getter for the parents_.
 */
std::vector<ae::ClientParents> RegistratorConfig::GetParents() {
  return parents_;
}

/**
 * \brief Getter for the servers_.
 */
std::vector<ae::ServerConfig> RegistratorConfig::GetServers() {
  return servers_;
}

/**
 * \brief Getter for the clients_total_.
 */
std::uint8_t RegistratorConfig::GetClientsTotal() { return clients_total_; }
}  // namespace ae
