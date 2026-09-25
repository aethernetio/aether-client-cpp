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

#ifndef AETHER_MODEMS_ESP32_MODEM_BOOT_H_
#define AETHER_MODEMS_ESP32_MODEM_BOOT_H_

#include "aether/config.h"
#if defined(ESP_PLATFORM) && defined(AE_MODEM_PWR_GPIO) && \
    defined(AE_MODEM_DTR_GPIO)
#  include <cassert>
#  include <chrono>
#  include <memory>
#  include <type_traits>
#  include <utility>
#  include "aether/ae_context.h"
#  include "aether/executors/executors.h"
#  include "aether/serial_ports/at_support/at_request.h"
#  include "aether/tele.h"
#  include "driver/gpio.h"

namespace ae::esp32_modem_boot_internal {
using Sender = ex::AnySender<ex::set_value_t(), ex::set_error_t(int),
                             ex::set_error_t(ex::TimeoutError)>;
inline constexpr auto kPowerPin = static_cast<gpio_num_t>(AE_MODEM_PWR_GPIO);
inline constexpr auto kDtrPin = static_cast<gpio_num_t>(AE_MODEM_DTR_GPIO);

// The Waveshare HAT inverts PWR through a transistor: high presses PWRKEY.
// Release the key even if the operation is cancelled during the pulse.
struct PowerKeyRelease {
  ~PowerKeyRelease() { gpio_set_level(kPowerPin, 0); }
};

inline auto Pause(AeContext context, std::chrono::milliseconds delay) {
  return ex::create<ex::set_value_t()>(
      [context, delay, task = TaskSubscription{}](auto& ctx) mutable noexcept {
        task = context.scheduler().DelayedTask(
            [&ctx]() noexcept { ex::set_value(std::move(ctx.receiver)); },
            delay);
        if (!task) {
          AE_TELED_ERROR("Failed to schedule modem boot delay");
          assert(false && "Task allocation failed");
        }
      });
}

inline Sender Probe(AeContext context, AtSupport& at) {
  return Sender{at::MakeRequest(ex::just(), at, "AT", at::Wait{"OK"}) |
                ex::with_timeout(context, std::chrono::seconds{1})};
}

inline Sender WaitReady(AeContext context, AtSupport& at, int attempts) {
  return Sender{
      Probe(context, at) |
      ex::let_error([context, &at, attempts](auto error) -> Sender {
        if constexpr (std::is_same_v<decltype(error), std::exception_ptr>) {
          return Sender{ex::just_error(error)};
        }
        if (attempts == 0) {
          return Sender{ex::just_error(error)};
        }
        return Sender{Pause(context, std::chrono::milliseconds{500}) |
                      ex::let_value([context, &at, attempts]() noexcept {
                        return WaitReady(context, at, attempts - 1);
                      })};
      })};
}

inline Sender Pulse(AeContext context) {
  return Sender{ex::create<ex::set_value_t(), ex::set_error_t(int)>(
      [context, release = std::unique_ptr<PowerKeyRelease>{},
       task = TaskSubscription{}](auto& ctx) mutable noexcept {
        release = std::make_unique<PowerKeyRelease>();
        auto error = gpio_set_level(kPowerPin, 1);
        if (error != ESP_OK) {
          ex::set_error(std::move(ctx.receiver), static_cast<int>(error));
          return;
        }
        task = context.scheduler().DelayedTask(
            [&ctx]() noexcept {
              auto error = gpio_set_level(kPowerPin, 0);
              if (error != ESP_OK) {
                ex::set_error(std::move(ctx.receiver), static_cast<int>(error));
                return;
              }
              ex::set_value(std::move(ctx.receiver));
            },
            std::chrono::milliseconds{1100});
        if (!task) {
          AE_TELED_ERROR("Failed to schedule PWRKEY release");
          gpio_set_level(kPowerPin, 0);
          assert(false && "Task allocation failed");
          ex::set_error(std::move(ctx.receiver),
                        static_cast<int>(ESP_ERR_NO_MEM));
        }
      })};
}

inline Sender EnsureReady(AeContext context, AtSupport& at) {
  return Sender{
      ex::just() | ex::let_value([context, &at]() noexcept -> Sender {
        // DTR low keeps the UART awake. Set output latches before
        // enabling outputs.
        auto error = gpio_set_level(kPowerPin, 0);
        if (error == ESP_OK) {
          error = gpio_set_level(kDtrPin, 0);
        }
        if (error == ESP_OK) {
          error = gpio_set_direction(kPowerPin, GPIO_MODE_OUTPUT);
        }
        if (error == ESP_OK) {
          error = gpio_set_direction(kDtrPin, GPIO_MODE_OUTPUT);
        }
        if (error != ESP_OK) {
          return Sender{ex::just_error(static_cast<int>(error))};
        }
        // A running modem must not receive another power-key pulse on
        // ESP restart.
        return Sender{
            WaitReady(context, at, 2) |
            ex::let_error([context, &at](auto error) noexcept -> Sender {
              if constexpr (!std::is_same_v<decltype(error),
                                            ex::TimeoutError>) {
                return Sender{ex::just_error(error)};
              } else {
                return Sender{Pulse(context) |
                              ex::let_value([context, &at]() noexcept {
                                return WaitReady(context, at, 12);
                              })};
              }
            })};
      })};
}
}  // namespace ae::esp32_modem_boot_internal
#endif
#endif  // AETHER_MODEMS_ESP32_MODEM_BOOT_H_
