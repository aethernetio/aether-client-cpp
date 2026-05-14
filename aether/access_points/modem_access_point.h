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

#ifndef AETHER_ACCESS_POINTS_MODEM_ACCESS_POINT_H_
#define AETHER_ACCESS_POINTS_MODEM_ACCESS_POINT_H_

#include "aether/config.h"
#if AE_SUPPORT_MODEMS

#  include "aether/obj/obj.h"
#  include "aether/ae_context.h"
#  include "aether/events/events.h"
#  include "aether/actions/action2_.h"
#  include "aether/adapters/modem_adapter.h"
#  include "aether/events/event_subscription.h"
#  include "aether/access_points/access_point.h"

namespace ae {
class Aether;

class ModemConnectAction final : public a2::Action {
 public:
  using ConnectionEvent = Event<void(bool)>;

  explicit ModemConnectAction(AeContext const& ae_context,
                              IModemDriver& driver);

  ConnectionEvent::Subscriber connection_event();

 private:
  void Start();

  IModemDriver* driver_;
  ConnectionEvent connection_event_;
  Subscription start_sub_;
  TaskSubscription task_sub_;
};

class ModemAccessPoint final : public AccessPoint {
  AE_OBJECT(ModemAccessPoint, AccessPoint, 0)
  ModemAccessPoint() = default;

 public:
  ModemAccessPoint(ObjProp prop, ObjPtr<Aether> aether,
                   ModemAdapter::ptr modem_adapter);

  AE_OBJECT_REFLECT(AE_MMBRS(aether_, modem_adapter_));

  ModemConnectAction& Connect();
  IModemDriver& modem_driver();

  std::vector<ObjPtr<Channel>> GenerateChannels(
      ObjPtr<Server> const& server) override;

 private:
  Obj::ptr aether_;
  ModemAdapter::ptr modem_adapter_;
  std::optional<ModemConnectAction> connect_action_;
  Subscription connect_sub_;
};
}  // namespace ae
#endif

#endif  // AETHER_ACCESS_POINTS_MODEM_ACCESS_POINT_H_
