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

#ifndef AETHER_SERIAL_PORT_AT_COMM_SUPPORT_H_
#define AETHER_SERIAL_PORT_AT_COMM_SUPPORT_H_

#include <string>
#include <optional>

#include "aether/common.h"
#include "aether/actions/action.h"
#include "aether/types/data_buffer.h"
#include "aether/actions/action_ptr.h"
#include "aether/actions/action_context.h"
#include "aether/events/event_subscription.h"

#include "aether/serial_ports/iserial_port.h"

namespace ae {
class AtCommSupport {
 public:
  class WriteAction final : public Action<WriteAction> {
   public:
    using Action::Action;
    UpdateStatus Update();
  };

  class WaitForResponseAction final : public Action<WaitForResponseAction> {
   public:
    WaitForResponseAction(ActionContext action_context, ISerialPort& serial,
                          std::string expected, Duration timeout);

    UpdateStatus Update(TimePoint current_time);

    std::optional<DataBuffer> response() const;

   private:
    void DataRead(DataBuffer const& data);

    std::string expected_;
    Duration timeout_;
    TimePoint timeout_time_;
    Subscription data_read_sub_;
    std::optional<DataBuffer> response_;
    bool success_;
    bool error_;
  };

  static std::string PinToString(std::uint16_t pin);

  AtCommSupport(ActionContext action_context, ISerialPort& serial);

  ActionPtr<WriteAction> SendATCommand(std::string const& command);
  ActionPtr<WaitForResponseAction> WaitForResponse(std::string expected,
                                                   Duration timeout);

 private:
  ActionContext action_context_;
  ISerialPort* serial_;
};

} /* namespace ae */

#endif  // AETHER_LORA_MODULES_EBYTE_E22_400_LM_H_
