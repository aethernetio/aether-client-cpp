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

#include <memory>
#include <vector>
#include <bitset>
#include <thread>
#include <string_view>

#include "aether/serial_ports/at_comm_support.h"
#include "aether/modems/exponent_time.h"
#include "aether/serial_ports/serial_ports_tele.h"

namespace ae {
  
AtCommSupport::AtCommSupport(ISerialPort *serial)
    : serial_{serial} {};

void AtCommSupport::SendATCommand(const std::string& command) {
  AE_TELED_DEBUG("AT command: {}", command);
  std::vector<uint8_t> data(command.begin(), command.end());
  data.push_back('\r');  // Adding a carriage return symbols
  data.push_back('\n');
  serial_->Write(data);
}

bool AtCommSupport::WaitForResponse(const std::string& expected,
                                     Duration timeout) {
  // Simplified implementation of waiting for a response
  auto start = Now();
  auto exponent_time = ExponentTime(std::chrono::milliseconds{1},
                                    std::chrono::milliseconds{100});

  while (true) {
    if (auto response = serial_->Read(); response) {
      std::string_view response_str(
          reinterpret_cast<char const*>(response->data()), response->size());
      AE_TELED_DEBUG("AT response: {}", response_str);
      if (response_str.find(expected) != std::string::npos) {
        return true;
      }
      if (response_str.find("ERROR") != std::string::npos) {
        return false;
      }
    }

    auto elapsed = Now() - start;
    if (elapsed > timeout) {
      return false;
    }

    // sleep for some time while waiting for response but no more than timeout
    auto sleep_time = exponent_time.Next();
    sleep_time = (elapsed + sleep_time > timeout)
                     ? std::chrono::duration_cast<Duration>(timeout - elapsed)
                     : sleep_time;
    std::this_thread::sleep_for(sleep_time);
  }
}

std::string AtCommSupport::PinToString(const std::uint8_t pin[4]) {
  std::string result{};

  for (int i = 0; i < 4; ++i) {
    if (pin[i] > 9) {
      result = "ERROR";
      break;
    }
    result += static_cast<char>('0' + pin[i]);
  }

  return result;
}
}  // namespace ae
