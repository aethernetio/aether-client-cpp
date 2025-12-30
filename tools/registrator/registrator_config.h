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
#include <filesystem>

#include "aether/types/address.h"
#include "aether/types/server_id.h"

namespace ini {
class Section;
}

namespace ae::reg {
struct Aether {
  std::string ed25519_sign_key;
  std::string hydrogen_sign_key;
};

struct WifiAdapter {
  std::string ssid;
  std::string password;
};

struct RegServer {
  ServerId server_id;
  std::string address;
  std::uint16_t port;
  ae::Protocol protocol;
};

struct ClientConfig {
  std::string client_id;
  std::string parent_uid;
};

class RegistratorConfig {
 public:
  explicit RegistratorConfig(std::filesystem::path const& ini_file);

  std::optional<Aether> const& aether() const;
  std::optional<WifiAdapter> const& wifi_adapter() const;
  std::vector<RegServer> const& reg_servers() const;
  std::vector<ClientConfig> const& clients() const;

 private:
  void ParseConfig(std::filesystem::path const& ini_file);

  void ParseAether(ini::Section const& aether);
  void ParseWifiAdapter(ini::Section const& wifi_adapter);
  void ParseRegServer(ini::Section const& reg_server, std::string const& name);
  void ParseClient(ini::Section const& client, std::string const& name);

  std::optional<Aether> aether_;
  std::optional<WifiAdapter> wifi_adapter_;
  std::vector<RegServer> reg_servers_;
  std::vector<ClientConfig> clients_;
};
}  // namespace ae::reg

#endif  // TOOLS_REGISTRATOR_REGISTRATOR_CONFIG_H_
