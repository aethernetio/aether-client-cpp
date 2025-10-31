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

#ifndef TESTS_TEST_SERIAL_PORT_MOCK_AT_OBSERVER_H_
#define TESTS_TEST_SERIAL_PORT_MOCK_AT_OBSERVER_H_

#include <utility>

#include "aether/serial_ports/at_support/at_dispatcher.h"
#include "aether/serial_ports/at_support/at_buffer.h"

namespace ae::tests {
class MockAtObserver final : public IAtObserver {
 public:
  using ObservationCallback =
      std::function<void(AtBuffer&, AtBuffer::iterator)>;

  MockAtObserver() = default;

  void Observe(AtBuffer& buffer, AtBuffer::iterator pos) override {
    if (observation_callback_) {
      observation_callback_(buffer, pos);
    }
  }

  void SetObservationCallback(ObservationCallback callback) {
    observation_callback_ = std::move(callback);
  }

 private:
  ObservationCallback observation_callback_;
};
}  // namespace ae::tests

#endif  // TESTS_TEST_SERIAL_PORT_MOCK_AT_OBSERVER_H_
