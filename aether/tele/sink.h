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

#ifndef AETHER_TELE_SINK_H_
#define AETHER_TELE_SINK_H_

#ifndef AETHER_TELE_TELE_H_
#  error "Include tele.h instead"
#endif

#include <memory>
#include <cassert>

#include "aether/tele/itrap.h"
#include "aether/tele/levels.h"
#include "aether/tele/modules.h"

namespace ae::tele {

template <typename ConfigProvider>
class TeleSink {
 public:
  using ConfigProviderType = ConfigProvider;

  template <Level::underlined_t l, std::uint32_t m>
  using TeleConfig =
      typename ConfigProviderType::template StaticTeleConfig<l, m>;

  using EnvConfig = typename ConfigProviderType::StaticEnvConfig;

  static TeleSink& Instance() {
    static TeleSink sink;
    return sink;
  }

  constexpr void SetTrap(std::shared_ptr<ITrap> const& trap) { trap_ = trap; }

  constexpr auto const& trap() const { return trap_; }

 private:
  std::shared_ptr<ITrap> trap_;
};
}  // namespace ae::tele

#endif  // AETHER_TELE_SINK_H_
