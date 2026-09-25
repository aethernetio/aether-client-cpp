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

#include "aether/serial_ports/esp32_serial_port.h"
#if ESP32_SERIAL_PORT_ENABLED == 1
#  include <algorithm>
#  include <array>
#  include <cassert>
#  include <chrono>
#  include <string>
#  include "aether/tele.h"
#  include "soc/soc_caps.h"
namespace ae {
namespace esp32_serial_port_internal {
constexpr int kRxBufferSize = 8192;
constexpr int kTxBufferSize = 8192;
constexpr std::size_t kWriteChunkSize = 2048;
constexpr int kEventQueueSize = 32;
constexpr std::size_t kReadChunkSize = 2048;
constexpr auto kMinDataBits = static_cast<int>(kBits::kFiveBits);
constexpr auto kMaxDataBits = static_cast<int>(kBits::kEigthBits);
}  // namespace esp32_serial_port_internal

Esp32SerialPort::Esp32SerialPort(AeContext const& context,
                                 SerialInit const& init)
    : context_{context} {
  uart_num_ = OpenPort(init, events_);
  if (IsOpen()) {
    Schedule();
  }
}
Esp32SerialPort::~Esp32SerialPort() { Close(); }
uart_port_t Esp32SerialPort::OpenPort(SerialInit const& init,
                                      QueueHandle_t& events) {
  auto port = UART_NUM_MAX;
  // UART enum constants are not macros. Exclude the low-power UART.
  for (int i = 0; i < SOC_UART_HP_NUM; ++i) {
    if (init.port_name == "UART" + std::to_string(i) ||
        init.port_name == "/dev/uart/" + std::to_string(i)) {
      port = static_cast<uart_port_t>(i);
      break;
    }
  }
  auto bits = static_cast<int>(init.byte_size);
  if (port == UART_NUM_MAX || bits < esp32_serial_port_internal::kMinDataBits ||
      bits > esp32_serial_port_internal::kMaxDataBits ||
      static_cast<int>(init.baud_rate) <= 0 ||
      (init.parity != kParity::kNoParity &&
       init.parity != kParity::kOddParity &&
       init.parity != kParity::kEvenParity) ||
      static_cast<int>(init.stop_bits) < 1 ||
      static_cast<int>(init.stop_bits) > 3 || uart_is_driver_installed(port)) {
    AE_TELED_ERROR("Invalid or occupied UART: {}", init.port_name);
    return UART_NUM_MAX;
  }
  uart_config_t config{};
  config.baud_rate = static_cast<int>(init.baud_rate);
  config.data_bits = static_cast<uart_word_length_t>(
      bits - esp32_serial_port_internal::kMinDataBits);
  config.parity = UART_PARITY_DISABLE;
  if (init.parity == kParity::kOddParity) {
    config.parity = UART_PARITY_ODD;
  } else if (init.parity == kParity::kEvenParity) {
    config.parity = UART_PARITY_EVEN;
  }
  config.stop_bits = static_cast<uart_stop_bits_t>(init.stop_bits);
  config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
  config.source_clk = UART_SCLK_DEFAULT;
  if (uart_param_config(port, &config) != ESP_OK ||
      uart_set_pin(port, init.tx_io_num, init.rx_io_num, UART_PIN_NO_CHANGE,
                   UART_PIN_NO_CHANGE) != ESP_OK ||
      uart_driver_install(port, esp32_serial_port_internal::kRxBufferSize,
                          esp32_serial_port_internal::kTxBufferSize,
                          esp32_serial_port_internal::kEventQueueSize, &events,
                          0) != ESP_OK) {
    AE_TELED_ERROR("Failed to configure UART: {}", init.port_name);
    return UART_NUM_MAX;
  }
  return port;
}
void Esp32SerialPort::Schedule() {
  task_ = context_.scheduler().DelayedTask([this] { Poll(); },
                                           std::chrono::milliseconds{2});
  if (!task_) {
    AE_TELED_ERROR("Failed to schedule UART polling");
    assert(false && "Task allocation failed");
    Close();
  }
}
void Esp32SerialPort::Poll() {
  uart_event_t event{};
  while (xQueueReceive(events_, &event, 0) == pdTRUE) {
    if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL ||
        event.type == UART_FRAME_ERR || event.type == UART_PARITY_ERR) {
      AE_TELED_ERROR("UART receive error: {}", static_cast<int>(event.type));
      Close();
      return;
    }
  }
  FlushWrite();
  if (!IsOpen()) {
    return;
  }
  std::array<std::uint8_t, esp32_serial_port_internal::kReadChunkSize> buffer{};
  auto count = uart_read_bytes(uart_num_, buffer.data(), buffer.size(), 0);
  if (count < 0) {
    AE_TELED_ERROR("UART read failed");
    Close();
    return;
  }
  // A subscriber can close the port, so schedule before emitting.
  Schedule();
  if (count > 0) {
    read_event_.Emit({buffer.data(), static_cast<std::size_t>(count)});
  }
}
void Esp32SerialPort::Write(std::span<std::uint8_t const> data) {
  if (!IsOpen()) {
    AE_TELED_ERROR("UART is closed");
    return;
  }
  constexpr std::size_t kMaxPendingBytes = 16384;
  if (data.size() > kMaxPendingBytes - pending_write_.size()) {
    AE_TELED_ERROR("UART transmit queue overflow");
    Close();
    return;
  }
  pending_write_.insert(pending_write_.end(), data.begin(), data.end());
  FlushWrite();
}
void Esp32SerialPort::FlushWrite() {
  if (pending_write_.empty()) {
    return;
  }
  // Only enqueue into an idle driver. A bounded chunk fits in the TX ring,
  // so uart_write_bytes does not wait for space while the scheduler is running.
  auto ready = uart_wait_tx_done(uart_num_, 0);
  if (ready == ESP_ERR_TIMEOUT) {
    return;
  }
  if (ready != ESP_OK) {
    AE_TELED_ERROR("UART TX status failed: {}", ready);
    Close();
    return;
  }
  auto size = std::min(pending_write_.size(),
                       esp32_serial_port_internal::kWriteChunkSize);
  std::size_t free_size{};
  if (uart_get_tx_buffer_free_size(uart_num_, &free_size) != ESP_OK) {
    AE_TELED_ERROR("UART TX buffer query failed");
    Close();
    return;
  }
  if (free_size < size) {
    return;
  }
  auto count = uart_write_bytes(uart_num_, pending_write_.data(), size);
  if (count < 0 || static_cast<std::size_t>(count) != size) {
    AE_TELED_ERROR("UART buffered write accepted {} of {} bytes", count, size);
    Close();
    return;
  }
  AE_TELED_DEBUG("UART TX buffer accepted {} bytes", count);
  pending_write_.erase(pending_write_.begin(), pending_write_.begin() + count);
}
Esp32SerialPort::DataReadEvent::Subscriber Esp32SerialPort::read_event() {
  return EventSubscriber{read_event_};
}
bool Esp32SerialPort::IsOpen() { return uart_num_ != UART_NUM_MAX; }
void Esp32SerialPort::Close() {
  task_.Reset();
  if (IsOpen()) {
    uart_driver_delete(uart_num_);
    uart_num_ = UART_NUM_MAX;
    events_ = nullptr;
  }
  pending_write_.clear();
}
}  // namespace ae
#endif
