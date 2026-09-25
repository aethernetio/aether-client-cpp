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

#include <unity.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <deque>
#include <vector>
#include "aether/modems/esp32_modem_boot.h"
#include "aether/serial_ports/esp32_serial_port.h"
#include "tests/test-serial-port/mock-serial-port.h"

namespace ae::test_esp32_serial_port {
struct Fake {
  bool installed{};
  int installs{}, deletes{}, reads{}, tx_pin{}, rx_pin{}, power_high{};
  int pin_error{};
  std::size_t tx_capacity{8192};
  int tx_buffer_size{};
  bool tx_busy{};
  std::size_t tx_free{8192};
  uart_port_t port{UART_NUM_MAX};
  uart_config_t config{};
  std::vector<std::uint8_t> received, transmitted;
  std::deque<uart_event_type_t> events;
  std::array<int, 32> levels{};
};
Fake fake;
struct Context {
  AeCtx ToAeContext() const {
    static constexpr auto table =
        AeCtxTable{nullptr, [](void* p) -> TaskScheduler& {
                     return static_cast<Context*>(p)->scheduler;
                   }};
    return {this, &table};
  }
  void Tick() {
    scheduler.Update(std::chrono::system_clock::now() +
                     std::chrono::milliseconds{10});
  }
  TaskScheduler scheduler;
};
SerialInit Init() {
  return {.port_name = "UART1", .tx_io_num = 16, .rx_io_num = 17};
}
void test_ConfigAndReopen() {
  Context c;
  {
    Esp32SerialPort port{c, Init()};
    TEST_ASSERT_TRUE(port.IsOpen());
    TEST_ASSERT_EQUAL_INT(UART_NUM_1, fake.port);
    TEST_ASSERT_EQUAL_INT(16, fake.tx_pin);
    TEST_ASSERT_EQUAL_INT(17, fake.rx_pin);
    TEST_ASSERT_EQUAL_INT(115200, fake.config.baud_rate);
    TEST_ASSERT_EQUAL_INT(8192, fake.tx_buffer_size);
    port.Close();
    port.Close();
    TEST_ASSERT_EQUAL_INT(1, fake.deletes);
    c.Tick();
    TEST_ASSERT_EQUAL_INT(0, fake.reads);
  }
  Esp32SerialPort reopened{c, Init()};
  TEST_ASSERT_TRUE(reopened.IsOpen());
  TEST_ASSERT_EQUAL_INT(2, fake.installs);
}
void test_InvalidAndOccupiedPort() {
  Context c;
  auto init = Init();
  init.port_name = "UART2";
  Esp32SerialPort invalid{c, init};
  TEST_ASSERT_FALSE(invalid.IsOpen());
  Esp32SerialPort valid{c, Init()};
  {
    Esp32SerialPort occupied{c, Init()};
    TEST_ASSERT_FALSE(occupied.IsOpen());
  }
  TEST_ASSERT_EQUAL_INT(0, fake.deletes);
  TEST_ASSERT_TRUE(valid.IsOpen());
}
void test_BufferedWritesKeepOrderWhileBusy() {
  Context c;
  Esp32SerialPort port{c, Init()};
  std::array<std::uint8_t, 5> first{1, 2, 3, 4, 5};
  std::array<std::uint8_t, 2> second{6, 7};
  port.Write(first);
  fake.tx_busy = true;
  port.Write(second);
  c.Tick();
  TEST_ASSERT_EQUAL_UINT(first.size(), fake.transmitted.size());
  fake.tx_busy = false;
  fake.tx_free = 0;
  c.Tick();
  TEST_ASSERT_EQUAL_UINT(first.size(), fake.transmitted.size());
  fake.tx_free = 8192;
  for (int i = 0; i < 5; ++i) {
    c.Tick();
  }
  std::array<std::uint8_t, 7> expected{1, 2, 3, 4, 5, 6, 7};
  TEST_ASSERT_EQUAL_UINT(expected.size(), fake.transmitted.size());
  TEST_ASSERT_EQUAL_MEMORY(expected.data(), fake.transmitted.data(),
                           expected.size());
}
void test_CloseInReadCallback() {
  Context c;
  Esp32SerialPort port{c, Init()};
  int calls = 0;
  Subscription sub = port.read_event().Subscribe([&](auto data) {
    ++calls;
    TEST_ASSERT_EQUAL_UINT(3, data.size());
    port.Close();
  });
  fake.received = {1, 2, 3};
  c.Tick();
  c.Tick();
  TEST_ASSERT_EQUAL_INT(1, calls);
  TEST_ASSERT_EQUAL_INT(1, fake.reads);
}
void test_BinaryPacketAcrossFifoBoundary() {
  Context c;
  Esp32SerialPort port{c, Init()};
  std::array<std::uint8_t, 152> packet{};
  for (std::size_t i = 0; i < packet.size(); ++i) {
    packet[i] = static_cast<std::uint8_t>(i);
  }
  port.Write(packet);
  TEST_ASSERT_EQUAL_UINT(packet.size(), fake.transmitted.size());
  c.Tick();
  TEST_ASSERT_EQUAL_UINT(packet.size(), fake.transmitted.size());
  TEST_ASSERT_EQUAL_MEMORY(packet.data(), fake.transmitted.data(),
                           packet.size());
}
void test_ReceiveOverflowClosesPort() {
  Context c;
  Esp32SerialPort port{c, Init()};
  fake.events.push_back(UART_FIFO_OVF);
  c.Tick();
  TEST_ASSERT_FALSE(port.IsOpen());
}
void test_ShortBufferedWriteClosesPort() {
  Context c;
  Esp32SerialPort port{c, Init()};
  fake.tx_capacity = 128;
  std::array<std::uint8_t, 152> packet{};
  port.Write(packet);
  TEST_ASSERT_FALSE(port.IsOpen());
  c.Tick();
  TEST_ASSERT_EQUAL_UINT(128, fake.transmitted.size());
}
void test_TransmitOverflowClosesPort() {
  Context c;
  Esp32SerialPort port{c, Init()};
  fake.tx_capacity = 0;
  std::vector<std::uint8_t> data(16385);
  port.Write(data);
  TEST_ASSERT_FALSE(port.IsOpen());
}
void test_RunningModemDoesNotPulsePower() {
  Context c;
  tests::MockSerialPort serial;
  AtSupport at{serial};
  bool done = false;
  auto waiter =
      ex::AsyncWaiter{AeContext{c},
                      esp32_modem_boot_internal::EnsureReady(c, at) |
                          ex::then([&]() noexcept { done = true; }) |
                          ex::upon_error([](auto) noexcept {
                            TEST_FAIL_MESSAGE("Unexpected boot failure");
                          }),
                      [](auto&&) noexcept {}};
  constexpr std::array<std::uint8_t, 4> ok{'O', 'K', '\r', '\n'};
  serial.WriteOut(ok);
  TEST_ASSERT_TRUE(done);
  TEST_ASSERT_EQUAL_INT(0, fake.power_high);
  TEST_ASSERT_EQUAL_INT(0, fake.levels[8]);
}
void test_ColdBootPulsesAndWaitsForAt() {
  Context c;
  tests::MockSerialPort serial;
  AtSupport at{serial};
  bool done = false;
  bool failed = false;
  auto waiter =
      ex::AsyncWaiter{AeContext{c},
                      esp32_modem_boot_internal::EnsureReady(c, at) |
                          ex::then([&]() noexcept { done = true; }) |
                          ex::upon_error([&](auto) noexcept { failed = true; }),
                      [](auto&&) noexcept {}};
  auto later = std::chrono::system_clock::now() + std::chrono::seconds{2};
  for (int i = 0; i < 10 && fake.power_high == 0; ++i) {
    c.scheduler.Update(later);
  }
  TEST_ASSERT_EQUAL_INT(1, fake.power_high);
  TEST_ASSERT_EQUAL_INT(1, fake.levels[14]);
  TEST_ASSERT_FALSE(done);
  c.scheduler.Update(later);
  TEST_ASSERT_EQUAL_INT(0, fake.levels[14]);
  constexpr std::array<std::uint8_t, 4> ok{'O', 'K', '\r', '\n'};
  serial.WriteOut(ok);
  TEST_ASSERT_TRUE(done);
  TEST_ASSERT_FALSE(failed);
}
void test_CancelReleasesPowerKey() {
  Context c;
  tests::MockSerialPort serial;
  AtSupport at{serial};
  {
    auto waiter =
        ex::AsyncWaiter{AeContext{c},
                        esp32_modem_boot_internal::EnsureReady(c, at) |
                            ex::upon_error([](auto) noexcept {}),
                        [](auto&&) noexcept {}};
    auto later = std::chrono::system_clock::now() + std::chrono::seconds{2};
    for (int i = 0; i < 10 && fake.power_high == 0; ++i) {
      c.scheduler.Update(later);
    }
    TEST_ASSERT_EQUAL_INT(1, fake.levels[14]);
  }
  TEST_ASSERT_EQUAL_INT(0, fake.levels[14]);
  c.Tick();
}
void test_BootTimeoutIsReported() {
  Context c;
  tests::MockSerialPort serial;
  AtSupport at{serial};
  bool timed_out = false;
  auto waiter = ex::AsyncWaiter{
      AeContext{c},
      esp32_modem_boot_internal::EnsureReady(c, at) |
          ex::upon_error([&](auto) noexcept { timed_out = true; }),
      [](auto&&) noexcept {}};
  auto later = std::chrono::system_clock::now() + std::chrono::seconds{2};
  for (int i = 0; i < 40 && !timed_out; ++i) {
    c.scheduler.Update(later);
  }
  TEST_ASSERT_TRUE(timed_out);
  for (int i = 0; i < 4; ++i) {
    c.scheduler.Update(later);
  }
  TEST_ASSERT_EQUAL_INT(1, fake.power_high);
  TEST_ASSERT_EQUAL_INT(0, fake.levels[14]);
}
}  // namespace ae::test_esp32_serial_port

bool uart_is_driver_installed(uart_port_t) {
  return ae::test_esp32_serial_port::fake.installed;
}
esp_err_t uart_param_config(uart_port_t p, uart_config_t const* c) {
  auto& f = ae::test_esp32_serial_port::fake;
  f.port = p;
  f.config = *c;
  return ESP_OK;
}
esp_err_t uart_set_pin(uart_port_t, int tx, int rx, int, int) {
  auto& f = ae::test_esp32_serial_port::fake;
  f.tx_pin = tx;
  f.rx_pin = rx;
  return f.pin_error;
}
esp_err_t uart_driver_install(uart_port_t, int, int tx_buffer_size, int,
                              QueueHandle_t* q, int) {
  auto& f = ae::test_esp32_serial_port::fake;
  ++f.installs;
  f.tx_buffer_size = tx_buffer_size;
  f.installed = true;
  *q = &f;
  return ESP_OK;
}
esp_err_t uart_driver_delete(uart_port_t) {
  auto& f = ae::test_esp32_serial_port::fake;
  ++f.deletes;
  f.installed = false;
  return ESP_OK;
}
int uart_read_bytes(uart_port_t, void* out, std::uint32_t size, std::uint32_t) {
  auto& f = ae::test_esp32_serial_port::fake;
  ++f.reads;
  auto n = std::min<std::size_t>(size, f.received.size());
  if (n != 0) {
    std::memcpy(out, f.received.data(), n);
  }
  f.received.erase(f.received.begin(),
                   f.received.begin() + static_cast<std::ptrdiff_t>(n));
  return static_cast<int>(n);
}
esp_err_t uart_wait_tx_done(uart_port_t, std::uint32_t timeout) {
  TEST_ASSERT_EQUAL_UINT(0, timeout);
  return ae::test_esp32_serial_port::fake.tx_busy ? ESP_ERR_TIMEOUT : ESP_OK;
}
int uart_write_bytes(uart_port_t, void const* source, std::size_t size) {
  auto const* data = static_cast<std::uint8_t const*>(source);
  auto& f = ae::test_esp32_serial_port::fake;
  auto n = std::min<std::size_t>(size, f.tx_capacity);
  f.transmitted.insert(f.transmitted.end(), data, data + n);
  return static_cast<int>(n);
}
esp_err_t uart_get_tx_buffer_free_size(uart_port_t, std::size_t* size) {
  *size = ae::test_esp32_serial_port::fake.tx_free;
  return ESP_OK;
}
int xQueueReceive(QueueHandle_t, void* out, std::uint32_t) {
  auto& f = ae::test_esp32_serial_port::fake;
  if (f.events.empty()) {
    return 0;
  }
  static_cast<uart_event_t*>(out)->type = f.events.front();
  f.events.pop_front();
  return pdTRUE;
}
esp_err_t gpio_set_level(gpio_num_t p, std::uint32_t value) {
  auto& f = ae::test_esp32_serial_port::fake;
  f.levels[p] = static_cast<int>(value);
  if (p == 14 && value != 0) {
    ++f.power_high;
  }
  return ESP_OK;
}
esp_err_t gpio_set_direction(gpio_num_t, int) { return ESP_OK; }
void setUp() { ae::test_esp32_serial_port::fake = {}; }
void tearDown() {}
int test_esp32_serial_port() {
  // NOLINTNEXTLINE: suite entry imports test names for Unity.
  using namespace ae::test_esp32_serial_port;
  UNITY_BEGIN();
  RUN_TEST(test_ConfigAndReopen);
  RUN_TEST(test_InvalidAndOccupiedPort);
  RUN_TEST(test_BufferedWritesKeepOrderWhileBusy);
  RUN_TEST(test_ShortBufferedWriteClosesPort);
  RUN_TEST(test_BinaryPacketAcrossFifoBoundary);
  RUN_TEST(test_CloseInReadCallback);
  RUN_TEST(test_ReceiveOverflowClosesPort);
  RUN_TEST(test_TransmitOverflowClosesPort);
  RUN_TEST(test_RunningModemDoesNotPulsePower);
  RUN_TEST(test_ColdBootPulsesAndWaitsForAt);
  RUN_TEST(test_CancelReleasesPowerKey);
  RUN_TEST(test_BootTimeoutIsReported);
  return UNITY_END();
}
int main() { return test_esp32_serial_port(); }
