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

#ifndef AETHER_ADAPTERS_ADAPTER_H_
#define AETHER_ADAPTERS_ADAPTER_H_

#include <vector>
#include <memory>

#include "aether/obj/obj.h"
#include "aether/types/address.h"
#include "aether/actions/action.h"

#include "aether/transport/itransport_builder.h"

namespace ae {
/**
 * \brief Action makes all the preparation required to build transport for
 * specified address.
 */
class TransportBuilderAction : public Action<TransportBuilderAction> {
 public:
  using Action::Action;
  using Action::operator=;

  virtual ActionResult Update() = 0;

  virtual std::vector<std::unique_ptr<ITransportBuilder>> builders() = 0;
};

/**
 * \brief The transport builder factory.
 * It represents network adapter like gsm modem, wifi, ethernet or any other
 * network interface.
 * It must configure interface and provide transport builders for specified
 * address.
 */
class Adapter : public Obj {
  AE_OBJECT(Adapter, Obj, 0)

 protected:
  Adapter() = default;

 public:
#ifdef AE_DISTILLATION
  explicit Adapter(Domain* domain);
#endif  // AE_DISTILLATION

  AE_OBJECT_REFLECT()

  /**
   * \brief Provide an action creating transport builders for address
   */
  virtual ActionPtr<TransportBuilderAction> CreateTransport(
      UnifiedAddress const& address);
};

}  // namespace ae

#endif  // AETHER_ADAPTERS_ADAPTER_H_ */
