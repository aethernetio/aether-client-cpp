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

#  include "aether/misc/defer.h"
#  include "aether/serial_ports/serial_ports_tele.h"

namespace ae {
Esp32SerialPort::ReadAction::ReadAction(ActionContext action_context,
                                        Esp32SerialPort& serial_port)
    : Action{action_context}, serial_port_{&serial_port}, read_event_{} {
  if (serial_port_->uart_num_ == UART_NUM_MAX) {
    return;
  }
  auto poller = serial_port_->poller_.Lock();
  assert(poller);
  poll_sub_ =
      poller->Add({serial_port_->uart_num_})
          .Subscribe(
              MethodPtr<&Esp32SerialPort::ReadAction::ReadAction::PollEvent>{
                  this});
}

UpdateStatus Esp32SerialPort::ReadAction::Update() {
  if (read_event_) {
    for (auto const& b : buffers_) {
      serial_port_->read_event_.Emit(b);
    }
    buffers_.clear();
    read_event_ = false;
  }
  return {};
}

void Esp32SerialPort::ReadAction::PollEvent(PollerEvent event) {
  if (event.descriptor != DescriptorType{serial_port_->uart_num_}) {
    return;
  }
  switch (event.event_type) {
    case EventType::kRead:
      ReadData();
      break;
    default:
      break;
  }
}

void Esp32SerialPort::ReadAction::ReadData() {
  if (serial_port_->uart_num_ == UART_NUM_MAX) {
    AE_TELE_ERROR(kAdapterSerialNotOpen, "Port is not open");

    return;
  }

  size_t length = 0;
  esp_err_t err = uart_get_buffered_data_len(serial_port_->uart_num_, &length);
  if (err != ESP_OK) {
    AE_TELE_ERROR(kAdapterSerialReadFailed, "Read failed {}!", err);
    return;
  }

  if (length == 0) {
    return;
  }

  DataBuffer buffer(length);
  int bytes_read =
      uart_read_bytes(serial_port_->uart_num_, buffer.data(), length, 0);

  // Reading error
  if (bytes_read <= 0) {
    AE_TELE_ERROR(kAdapterSerialReadFailed, "Read failed, no data!");
    return;
  } else {
    buffer.resize(bytes_read);

    AE_TELED_DEBUG("Serial data read {} bytes: {}", bytes_read, buffer);

    buffers_.emplace_back(std::move(buffer));
    read_event_ = true;
    Action::Trigger();
  }
}

Esp32SerialPort::Esp32SerialPort(ActionContext action_context,
                                 SerialInit serial_init,
                                 IPoller::ptr const& poller)
    : action_context_{action_context},
      serial_init_{std::move(serial_init)},
      poller_{poller},
      uart_num_{OpenPort(serial_init_)},
      read_action_{action_context_, *this} {}

Esp32SerialPort::~Esp32SerialPort() { Close(); }

void Esp32SerialPort::Write(DataBuffer const& data) {
  if (uart_num_ == UART_NUM_MAX) {
    AE_TELE_ERROR(kAdapterSerialNotOpen, "Port is not open");
    return;
  }

  int bytes_written = uart_write_bytes(uart_num_, data.data(), data.size());
  if (bytes_written < 0) {
    // Recording error
    AE_TELE_ERROR(kAdapterSerialWriteFailed, "Write failed!");
    Close();
  }
}

Esp32SerialPort::DataReadEvent::Subscriber Esp32SerialPort::read_event() {
  return EventSubscriber{read_event_};
}

bool Esp32SerialPort::IsOpen() { return uart_num_ != UART_NUM_MAX; }

uart_port_t Esp32SerialPort::OpenPort(SerialInit const& serial_init) {
  uart_port_t uart_num{UART_NUM_MAX};

  if (!GetUartNumber(serial_init.port_name, &uart_num)) {
    return UART_NUM_MAX;
  }

  auto close_on_exit = defer_at[&] { uart_driver_delete(uart_num); };

  if (!SetOptions(uart_num, serial_init)) {
    return UART_NUM_MAX;
  }

  close_on_exit.Reset();

  return uart_num;
}

bool Esp32SerialPort::SetOptions(uart_port_t uart_num,
                                 SerialInit const& serial_init) {
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

esp_err_t Esp32SerialPort::SetupTimeouts() {
  return uart_set_rx_timeout(uart_num_, 10);  // 10 units ? 100 ms
}

bool Esp32SerialPort::GetUartNumber(const std::string& port_name,
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

void Esp32SerialPort::Close() {
  if (uart_num_ != UART_NUM_MAX) {
    uart_driver_delete(uart_num_);
  }
}
} /* namespace ae */

#endif  // ESP32_SERIAL_PORT_ENABLED
