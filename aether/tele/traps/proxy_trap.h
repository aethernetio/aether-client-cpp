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

  void LogLine(Tag const& tag, ILogCollector& log_collector) override {
    first->LogLine(tag, log_collector);
    second->LogLine(tag, log_collector);
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
