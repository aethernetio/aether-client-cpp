/*
 * Copyright 2026 Aethernet Inc.
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

#include <iostream>

#include "aether/config.h"

#if defined(_WIN32)
#  include "aether-objects/domain_storage/ram_domain_storage.h"
#  include "aether-objects/obj/domain.h"
#  include "aether/serial_ports/win_serial_port.h"

namespace ae::test_win_serial_reopen {
struct TestContext {
  AeCtx ToAeContext() const {
    static constexpr auto table =
        AeCtxTable{nullptr, [](void* obj) -> TaskScheduler& {
                     return static_cast<TestContext*>(obj)->scheduler;
                   }};
    return AeCtx{this, &table};
  }
  TaskScheduler scheduler;
};
}  // namespace ae::test_win_serial_reopen
#endif

// Opt-in hardware check: opens the port without sending any AT commands.
int test_win_serial_reopen(char const* port) {
#if defined(_WIN32)
  ae::test_win_serial_reopen::TestContext context;
  ae::RamDomainStorage storage;
  ae::Domain domain{storage};
  ae::SerialInit init;
  init.port_name = port;
  for (int i = 0; i < 100; ++i) {
    ae::IPoller::ptr poller = ae::WinPoller::ptr::Create(domain);
    ae::WinSerialPort serial{context, init, poller};
    if (!serial.IsOpen()) {
      std::cerr << "Serial reopen failed at iteration " << i << '\n';
      return 1;
    }
    serial.Close();
    serial.Close();
    if (serial.IsOpen()) {
      std::cerr << "Closed serial port still reports open\n";
      return 1;
    }
    // The old port object is still alive, just like a retained modem driver.
    // The prepared send stage uses a separate context and poller.
    ae::IPoller::ptr next_poller = ae::WinPoller::ptr::Create(domain);
    ae::WinSerialPort reopened{context, init, next_poller};
    if (!reopened.IsOpen()) {
      std::cerr << "Reopen with old object alive failed at iteration " << i
                << '\n';
      return 1;
    }
  }
  std::cout << "100 immediate serial reopen cycles passed\n";
  return 0;
#else
  (void)port;
  std::cerr << "Serial reopen check requires Windows\n";
  return 2;
#endif
}
