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

#  include "aether/serial_ports/serial_ports_tele.h"

namespace ae {
ESP32SerialPort::ESP32SerialPort(SerialInit const& serial_init)
    : is_open_(false) {
  is_open_ = Initialize(serial_init);
}

ESP32SerialPort::~ESP32SerialPort() { Close(); }

void ESP32SerialPort::Write(DataBuffer const& data) {
  if (!is_open_) return;

  int bytes_written = uart_write_bytes(uart_num_, data.data(), data.size());
  if (bytes_written < 0) {
    // Recording error
    AE_TELE_ERROR(kAdapterSerialWriteFailed, "Write failed!");
    Close();
  }
}

std::optional<DataBuffer> ESP32SerialPort::Read() {
  if (!is_open_) return std::nullopt;

  size_t length = 0;
  esp_err_t err = uart_get_buffered_data_len(uart_num_, &length);
  if (err != ESP_OK) {
    AE_TELE_ERROR(kAdapterSerialReadFailed, "Read failed {}!", err);
    return std::nullopt;
  }

  if (length == 0) {
    return std::nullopt;
  }

  DataBuffer buffer(length);
  int bytes_read = uart_read_bytes(uart_num_, buffer.data(), length, 0);

  // Reading error
  if (bytes_read <= 0) {
    AE_TELE_ERROR(kAdapterSerialReadFailed, "Read failed, no data!");
    return std::nullopt;
  }

  buffer.resize(bytes_read);
  return buffer;
}

bool ESP32SerialPort::IsOpen() { return is_open_; }

bool ESP32SerialPort::Initialize(SerialInit const& serial_init) {
  if (!GetUartNumber(serial_init.port_name, &uart_num_)) {
    return false;
  }

  uart_config_t uart_config = {
      /* baud_rate */ static_cast<int>(serial_init.baud_rate),
      /* data_bits */ UART_DATA_8_BITS,
      /* parity */ UART_PARITY_DISABLE,
      /* stop_bits */ UART_STOP_BITS_1,
      /* flow_ctrl */ UART_HW_FLOWCTRL_DISABLE,
      /* rx_flow_ctrl_thresh */ 122,
      /* source_clk */ UART_SCLK_DEFAULT,
      /* flags */ {}};

  if (uart_param_config(uart_num_, &uart_config) != ESP_OK) {
    return false;
  }

  if (uart_set_pin(uart_num_, serial_init.tx_io_num, serial_init.rx_io_num,
                   UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
    return false;
  }

  if (uart_driver_install(uart_num_,
                          // Receive buffer size
                          4096,
                          // Transmit Buffer size
                          0,
                          // Event Queue size
                          0,
                          // Event Queue Handler
                          NULL,
                          // Flags
                          0) != ESP_OK) {
    return false;
  }

  if (SetupTimeouts() != ESP_OK) {
    uart_driver_delete(uart_num_);
    return false;
  }

  return true;
}

bool ESP32SerialPort::GetUartNumber(const std::string& port_name,
                                    uart_port_t* out_uart_num) {
  if (port_name == "UART0" || port_name == "/dev/uart/0") {
#  if defined UART_NUM_0
    *out_uart_num = UART_NUM_0;
#  endif
    return true;
  } else if (port_name == "UART1" || port_name == "/dev/uart/1") {
#  if defined UART_NUM_1
    *out_uart_num = UART_NUM_1;
#  endif
    return true;
  } else if (port_name == "UART2" || port_name == "/dev/uart/2") {
#  if defined UART_NUM_2
    *out_uart_num = UART_NUM_2;
#  endif
    return true;
  }
  return false;
}

esp_err_t ESP32SerialPort::SetupTimeouts() {
  return uart_set_rx_timeout(uart_num_, 10);  // 10 units ? 100 ms
}

void ESP32SerialPort::Close() {
  if (is_open_) {
    uart_driver_delete(uart_num_);
    is_open_ = false;
  }
}
} /* namespace ae */

#endif  // ESP32_SERIAL_PORT_ENABLED
