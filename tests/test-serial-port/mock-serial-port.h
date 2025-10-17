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

#ifndef TESTS_TEST_SERIAL_PORT_MOCK_SERIAL_PORT_H_
#define TESTS_TEST_SERIAL_PORT_MOCK_SERIAL_PORT_H_

#include "aether/types/data_buffer.h"
#include "aether/events/events.h"

#include "aether/serial_ports/iserial_port.h"

namespace ae::tests {
class MockSerialPort final : public ISerialPort {
 public:
  MockSerialPort() = default;

  bool IsOpen() override { return true; }

  void Write(DataBuffer const& data) override { write_event_.Emit(data); }

  DataReadEvent::Subscriber read_event() override {
    return EventSubscriber{read_event_};
  }

  // calls read_event
  void WriteOut(DataBuffer const& data) { read_event_.Emit(data); }

  Event<void(DataBuffer const& data)>::Subscriber write_event() {
    return EventSubscriber{write_event_};
  }

 private:
  DataReadEvent read_event_;
  Event<void(DataBuffer const& data)> write_event_;
};
}  // namespace ae::tests

#endif  // TESTS_TEST_SERIAL_PORT_MOCK_SERIAL_PORT_H_
