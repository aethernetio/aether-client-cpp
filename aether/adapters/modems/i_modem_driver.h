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

#ifndef AETHER_ADAPTERS_MODEMS_I_MODEM_DRIVER_H_
#define AETHER_ADAPTERS_MODEMS_I_MODEM_DRIVER_H_

#include <functional>
#include <string>

namespace ae {

// A class for managing event subscriptions
template <typename... Args>
class EventSubscriberModem {
public:
    void subscribe(std::function<void(Args...)> callback) {
        callbacks_.push_back(std::move(callback));
    }

    std::vector<std::function<void(Args...)>> GetCallbacks() {
      return callbacks_;
    }

private:
    friend class EventEmitter;
    std::vector<std::function<void(Args...)>> callbacks_;
};

// A class for generating events
template <typename... Args>
class EventEmitterModem {
public:
    EventSubscriberModem<Args...>& subscriber() {
        return subscriber_;
    }

    void notify(Args... args) {
      auto callbacks = subscriber_.GetCallbacks();
      for (auto& callback : callbacks) {
            callback(args...);
        }
    }

private:
    EventSubscriberModem<Args...> subscriber_;
};

class IModemDriver {
public:
    virtual ~IModemDriver() = default;
    
    virtual void Init() = 0;
    virtual void Setup() = 0;
    virtual void Stop() = 0;
    
    virtual EventSubscriberModem<>& initiated() = 0;
    virtual EventSubscriberModem<const std::string&>& errorOccurred() = 0;
};

} /* namespace ae */

#endif  // AETHER_ADAPTERS_MODEMS_I_MODEM_DRIVER_H_