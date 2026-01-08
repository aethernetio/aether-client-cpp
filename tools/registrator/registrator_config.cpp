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

#include "registrator/registrator_config.h"

#include "third_party/ini.h/ini.h"

#include <charconv>
#include <string_view>

namespace ae::reg {

/**
 * \brief Class constructor.
 */
RegistratorConfig::RegistratorConfig(std::filesystem::path const& ini_file) {
  ParseConfig(ini_file);
};

std::optional<Aether> const& RegistratorConfig::aether() const {
  return aether_;
}
std::optional<WifiAdapter> const& RegistratorConfig::wifi_adapter() const {
  return wifi_adapter_;
}
std::vector<RegServer> const& RegistratorConfig::reg_servers() const {
  return reg_servers_;
}
std::vector<ClientConfig> const& RegistratorConfig::clients() const {
  return clients_;
}

void RegistratorConfig::ParseConfig(std::filesystem::path const& ini_file) {
  // Reading settings from the ini file.
  ini::File file = ini::open(ini_file);

  for (auto const& [name, section] : file) {
    if (name == "Aether") {
      ParseAether(section);
    }
    if (name == "WifiAdapter") {
      ParseWifiAdapter(section);
    }
    if (name.find("RegServer") != std::string::npos) {
      ParseRegServer(section, name);
    }
    if (name.find("Client") != std::string::npos) {
      ParseClient(section, name);
    }
  }
}

void RegistratorConfig::ParseAether(ini::Section const& aether) {
  aether_.emplace();

  if (aether.has_key("ed25519_sign_key")) {
    aether_->ed25519_sign_key = aether.get<std::string>("ed25519_sign_key");
  }
  if (aether.has_key("hydrogen_sign_key")) {
    aether_->hydrogen_sign_key = aether.get<std::string>("hydrogen_sign_key");
  }
}

void RegistratorConfig::ParseWifiAdapter(ini::Section const& wifi_adapter) {
  wifi_adapter_.emplace();
  assert(wifi_adapter.has_key("ssid"));
  assert(wifi_adapter.has_key("pass"));
  wifi_adapter_->ssid = wifi_adapter.get<std::string>("ssid");
  wifi_adapter_->password = wifi_adapter.get<std::string>("pass");
}

void RegistratorConfig::ParseRegServer(ini::Section const& reg_server,
                                       std::string const& name) {
  RegServer server;

  // parse name and get id
  auto pos = name.find('_');
  if (pos != std::string::npos) {
    auto suffix = std::string_view{name}.substr(pos + 1);
    auto res = std::from_chars(suffix.data(), suffix.data() + suffix.size(),
                               server.server_id);
  }
  assert(reg_server.has_key("address"));
  assert(reg_server.has_key("port"));
  assert(reg_server.has_key("protocol"));

  server.address = reg_server.get<std::string>("address");
  server.port = static_cast<std::uint16_t>(reg_server.get<int>("port"));

  auto protocol = reg_server.get<std::string>("protocol");
  static auto const protocols_map = std::map<std::string_view, Protocol>{
      {"kTcp", Protocol::kTcp},
      {"kUdp", Protocol::kUdp},
      {"kWebSocket", Protocol::kWebSocket}};
  auto p_it = protocols_map.find(protocol);
  assert((p_it != protocols_map.end()) && "Unknown protocol");
  server.protocol = p_it->second;

  reg_servers_.push_back(std::move(server));
}

void RegistratorConfig::ParseClient(ini::Section const& client,
                                    std::string const& name) {
  ClientConfig client_config{};
  // parser client id
  auto pos = name.find('_');
  assert((pos != std::string::npos) && "Invalid client name");
  client_config.client_id = name.substr(pos + 1);

  assert(client.has_key("parent_uid"));
  client_config.parent_uid = client.get<std::string>("parent_uid");

  clients_.emplace_back(std::move(client_config));
}

}  // namespace ae::reg
