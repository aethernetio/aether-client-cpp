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

#ifndef AETHER_TELE_TRAPS_PROXY_TRAP_H_
#define AETHER_TELE_TRAPS_PROXY_TRAP_H_

#include <memory>

#include "aether/common.h"

#include "aether/tele/itrap.h"

namespace ae::tele {
template <typename TrapFirst, typename TrapSecond>
class ProxyTrap final : public ITrap {
  static_assert(std::is_base_of_v<ITrap, TrapFirst> &&
                    std::is_base_of_v<ITrap, TrapSecond>,
                "TrapFirst and TrapSecond must be derived from ITrap");

 public:
  ProxyTrap(std::shared_ptr<TrapFirst> trap1, std::shared_ptr<TrapSecond> trap2)
      : first{std::move(trap1)}, second{std::move(trap2)} {}

  void AddInvoke(Tag const& tag, std::uint32_t count) override {
    first->AddInvoke(tag, count);
    second->AddInvoke(tag, count);
  }

  void AddInvokeDuration(Tag const& tag, Duration duration) override {
    first->AddInvokeDuration(tag, duration);
    second->AddInvokeDuration(tag, duration);
  }

  void OpenLogLine(Tag const& tag) override {
    first->OpenLogLine(tag);
    second->OpenLogLine(tag);
  }

  void InvokeTime(TimePoint time) override {
    first->InvokeTime(time);
    second->InvokeTime(time);
  }

  void WriteLevel(Level level) override {
    first->WriteLevel(level);
    second->WriteLevel(level);
  }

  void WriteModule(Module const& module) override {
    first->WriteModule(module);
    second->WriteModule(module);
  }

  void Location(std::string_view file, std::uint32_t line) override {
    first->Location(file, line);
    second->Location(file, line);
  }

  void TagName(std::string_view name) override {
    first->TagName(name);
    second->TagName(name);
  }

  void Blob(std::uint8_t const* data, std::size_t size) override {
    first->Blob(data, size);
    second->Blob(data, size);
  }

  void CloseLogLine(Tag const& tag) override {
    first->CloseLogLine(tag);
    second->CloseLogLine(tag);
  }

  void WriteEnvData(EnvData const& env_data) override {
    first->WriteEnvData(env_data);
    second->WriteEnvData(env_data);
  }

  std::shared_ptr<TrapFirst> first;
  std::shared_ptr<TrapSecond> second;
};
}  // namespace ae::tele

#endif  // AETHER_TELE_TRAPS_PROXY_TRAP_H_
