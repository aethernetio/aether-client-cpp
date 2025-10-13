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
#include <functional>
#include <string_view>

#include "aether/common.h"
#include "aether/actions/action.h"
#include "aether/misc/from_chars.h"
#include "aether/types/data_buffer.h"
#include "aether/actions/action_ptr.h"
#include "aether/actions/action_context.h"
#include "aether/events/event_subscription.h"

#include "aether/serial_ports/iserial_port.h"

namespace ae {
class AtCommSupport {
 public:
  class AtBuffer {
   public:
    using DataLines = std::list<DataBuffer>;
    using iterator = DataLines::iterator;
    using UpdateEvent = Event<void(iterator it)>;

    explicit AtBuffer(ISerialPort& serial_port);

    UpdateEvent::Subscriber update_event();

    iterator FindPattern(std::string_view str);
    iterator FindPattern(std::string_view str, iterator start);

    iterator begin();
    iterator end();
    iterator erase(iterator it);
    iterator erase(iterator first, iterator last);

   private:
    void DataRead(DataBuffer const& data);

    std::list<DataBuffer> data_lines_;
    UpdateEvent update_event_;
    Subscription data_read_sub_;
  };

  class WriteAction final : public Action<WriteAction> {
   public:
    using Action::Action;
    UpdateStatus Update();
  };

  class WaitForResponseAction final : public Action<WaitForResponseAction> {
   public:
    using Handler = std::function<UpdateStatus(
        AtBuffer& buffer, AtBuffer::iterator response_pos)>;

    WaitForResponseAction(
        ActionContext action_context, AtBuffer& buffer, std::string expected,
        Duration timeout,
        Handler response_handler = [](AtBuffer& buffer,
                                      AtBuffer::iterator response_pos) {
          buffer.erase(response_pos);
          return UpdateStatus::Result();
        });

    UpdateStatus Update(TimePoint current_time);

   private:
    void DataRead(AtBuffer::iterator pos);

    std::string expected_;
    Duration timeout_;
    TimePoint timeout_time_;
    AtBuffer* buffer_;
    AtBuffer::iterator response_pos_;
    Subscription data_update_sub_;
    bool success_;
    bool error_;
    Handler response_handler_;
  };

  class AtListener {
   public:
    using Handler = std::function<AtBuffer::iterator(AtBuffer& buffer,
                                                     AtBuffer::iterator pos)>;

    AtListener(AtBuffer& buffer, std::string expected, Handler handler);

   private:
    void DataRead(AtBuffer::iterator it);

    std::string expected_;
    AtBuffer* buffer_;
    Handler handler_;
    Subscription data_update_sub_;
  };

  static std::string PinToString(std::uint16_t pin);

  AtCommSupport(ActionContext action_context, ISerialPort& serial);

  ActionPtr<WriteAction> SendATCommand(std::string const& command);
  ActionPtr<WaitForResponseAction> WaitForResponse(std::string expected,
                                                   Duration timeout);

  ActionPtr<WaitForResponseAction> WaitForResponse(
      std::string expected, Duration timeout,
      WaitForResponseAction::Handler response_handler);

  std::unique_ptr<AtListener> ListenForResponse(std::string expected,
                                                AtListener::Handler handler);

  template <typename... Ts>
  static bool ParseResponse(DataBuffer const& response, std::string_view cmd,
                            Ts&... args) {
    auto resp_str = std::string_view{
        reinterpret_cast<char const*>(response.data()), response.size()};
    auto start = resp_str.find(cmd);
    if (start == std::string_view::npos) {
      return false;
    }
    start += cmd.size() + 2;  // 2 for ": "
    auto end = start;
    bool res = true;
    (
        [&]() {
          end = resp_str.find(',', start);
          res = res && ParseArg(resp_str.substr(start, end - start), args);
          start = end + 1;
        }(),
        ...);
    return res;
  }

 private:
  static bool ParseArg(std::string_view arg, std::string& value) {
    value = std::string{arg};
    return true;
  }

  template <typename T>
  static bool ParseArg(std::string_view arg, T& value) {
    auto v = FromChars<T>(arg);
    if (!v.has_value()) {
      return false;
    }
    value = v.value();
    return true;
  }

  ActionContext action_context_;
  ISerialPort* serial_;
  AtBuffer at_buffer_;
};

} /* namespace ae */

#endif  // AETHER_LORA_MODULES_EBYTE_E22_400_LM_H_
