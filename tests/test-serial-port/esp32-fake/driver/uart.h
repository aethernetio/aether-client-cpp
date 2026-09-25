#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t = int;
inline constexpr int ESP_OK = 0;
inline constexpr int ESP_ERR_NO_MEM = 257;
inline constexpr int ESP_ERR_TIMEOUT = 263;
using QueueHandle_t = void*;
inline constexpr int pdTRUE = 1;
inline constexpr int UART_PIN_NO_CHANGE = -1;
enum uart_port_t { UART_NUM_0, UART_NUM_1, UART_NUM_MAX };
enum uart_word_length_t {
  UART_DATA_5_BITS,
  UART_DATA_6_BITS,
  UART_DATA_7_BITS,
  UART_DATA_8_BITS
};
enum uart_parity_t {
  UART_PARITY_DISABLE,
  UART_PARITY_EVEN = 2,
  UART_PARITY_ODD = 3
};
enum uart_stop_bits_t {
  UART_STOP_BITS_1 = 1,
  UART_STOP_BITS_1_5,
  UART_STOP_BITS_2
};
inline constexpr int UART_HW_FLOWCTRL_DISABLE = 0;
inline constexpr int UART_SCLK_DEFAULT = 1;
enum uart_event_type_t {
  UART_DATA,
  UART_FIFO_OVF,
  UART_BUFFER_FULL,
  UART_FRAME_ERR,
  UART_PARITY_ERR
};
struct uart_event_t {
  uart_event_type_t type;
};
struct uart_config_t {
  int baud_rate{};
  uart_word_length_t data_bits{};
  uart_parity_t parity{};
  uart_stop_bits_t stop_bits{};
  int flow_ctrl{};
  int source_clk{};
};
bool uart_is_driver_installed(uart_port_t);
esp_err_t uart_param_config(uart_port_t, uart_config_t const*);
esp_err_t uart_set_pin(uart_port_t, int, int, int, int);
esp_err_t uart_driver_install(uart_port_t, int, int, int, QueueHandle_t*, int);
esp_err_t uart_driver_delete(uart_port_t);
int uart_read_bytes(uart_port_t, void*, std::uint32_t, std::uint32_t);
esp_err_t uart_wait_tx_done(uart_port_t, std::uint32_t);
esp_err_t uart_get_tx_buffer_free_size(uart_port_t, std::size_t*);
int uart_write_bytes(uart_port_t, void const*, std::size_t);
int xQueueReceive(QueueHandle_t, void*, std::uint32_t);
