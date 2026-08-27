/*
 * Bare ESP-IDF Wi-Fi STA + UDP timing probe for ESP32-C6.
 * One source file. No Aether, prepared, sleep, static IP, or Wi-Fi hacks.
 */

#include <cstdio>
#include <cstring>
#include <inttypes.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "nvs_flash.h"

namespace {

constexpr char kTag[] = "wifi_timing";
constexpr char kSsid[] = "chirkov";
constexpr char kPass[] = "kcdjepWz51";
constexpr char kDestIp[] = "192.168.68.84";
constexpr uint16_t kDestPort = 3333;
constexpr int kMsgCount = 20;
constexpr int kIntervalMs = 500;

constexpr EventBits_t kGotIpBit = BIT0;

int64_t g_t0_us = 0;
int64_t g_sta_start_us = 0;
int64_t g_connect_call_us = 0;
int64_t g_sta_connected_us = 0;
int64_t g_got_ip_us = 0;
int g_disconnect_count = 0;
bool g_connect_called = false;

EventGroupHandle_t g_events = nullptr;

int64_t NowUs() { return esp_timer_get_time(); }

int64_t SinceStartUs() { return NowUs() - g_t0_us; }

void Mark(const char* name) {
  const int64_t t = SinceStartUs();
  ESP_LOGI(kTag, "MARK name=%s t_us=%" PRId64, name, t);
  std::printf("MARK name=%s t_us=%" PRId64 "\n", name, t);
  std::fflush(stdout);
}

void WifiEvent(void*, esp_event_base_t base, int32_t id, void* data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
    g_sta_start_us = SinceStartUs();
    Mark("WIFI_EVENT_STA_START");
    g_connect_call_us = SinceStartUs();
    Mark("esp_wifi_connect_call");
    g_connect_called = true;
    esp_wifi_connect();
    return;
  }

  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    ++g_disconnect_count;
    const auto* ev = static_cast<wifi_event_sta_disconnected_t*>(data);
    const int64_t t = SinceStartUs();
    ESP_LOGW(kTag,
             "MARK name=WIFI_EVENT_STA_DISCONNECTED t_us=%" PRId64
             " reason=%u attempt=%d",
             t, static_cast<unsigned>(ev->reason), g_disconnect_count);
    std::printf(
        "MARK name=WIFI_EVENT_STA_DISCONNECTED t_us=%" PRId64
        " reason=%u attempt=%d\n",
        t, static_cast<unsigned>(ev->reason), g_disconnect_count);
    std::fflush(stdout);
    esp_wifi_connect();
    return;
  }

  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
    g_sta_connected_us = SinceStartUs();
    Mark("WIFI_EVENT_STA_CONNECTED");
    return;
  }

  if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    g_got_ip_us = SinceStartUs();
    Mark("IP_EVENT_STA_GOT_IP");
    const auto* ev = static_cast<ip_event_got_ip_t*>(data);
    ESP_LOGI(kTag, "ip=" IPSTR " gw=" IPSTR,
             IP2STR(&ev->ip_info.ip), IP2STR(&ev->ip_info.gw));
    xEventGroupSetBits(g_events, kGotIpBit);
  }
}

void SendUdpBurst() {
  sockaddr_in dest{};
  dest.sin_family = AF_INET;
  dest.sin_port = htons(kDestPort);
  inet_pton(AF_INET, kDestIp, &dest.sin_addr);

  const int sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
  if (sock < 0) {
    ESP_LOGE(kTag, "socket failed");
    return;
  }

  char buf[192];
  for (int seq = 1; seq <= kMsgCount; ++seq) {
    const int64_t send_us = SinceStartUs();
    const int n = std::snprintf(
        buf, sizeof(buf),
        "seq=%d,app=%" PRId64 ",sta_start=%" PRId64 ",sta_connected=%" PRId64
        ",got_ip=%" PRId64,
        seq, send_us, g_sta_start_us, g_sta_connected_us, g_got_ip_us);
    ::sendto(sock, buf, n, 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
    ESP_LOGI(kTag, "MARK name=sendto seq=%d t_us=%" PRId64, seq, send_us);
    std::printf("MARK name=sendto seq=%d t_us=%" PRId64 "\n", seq, send_us);
    std::fflush(stdout);
    if (seq == 1) {
      Mark("first_sendto");
    }
    if (seq < kMsgCount) {
      vTaskDelay(pdMS_TO_TICKS(kIntervalMs));
    }
  }
  ::close(sock);
  Mark("udp_burst_done");
}

}  // namespace

extern "C" void app_main(void) {
  g_t0_us = NowUs();
  Mark("app_main");

  ESP_ERROR_CHECK(nvs_flash_init());
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_sta();

  g_events = xEventGroupCreate();

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiEvent, nullptr, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiEvent, nullptr, nullptr));

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  Mark("before_esp_wifi_init");
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  Mark("after_esp_wifi_init");

  wifi_config_t wifi_config = {};
  std::strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), kSsid,
               sizeof(wifi_config.sta.ssid));
  std::strncpy(reinterpret_cast<char*>(wifi_config.sta.password), kPass,
               sizeof(wifi_config.sta.password));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

  Mark("before_esp_wifi_start");
  ESP_ERROR_CHECK(esp_wifi_start());
  Mark("after_esp_wifi_start");

  xEventGroupWaitBits(g_events, kGotIpBit, pdFALSE, pdFALSE, portMAX_DELAY);

  const int64_t app_to_start = g_sta_start_us;
  const int64_t start_to_conn = g_sta_connected_us - g_sta_start_us;
  const int64_t conn_to_ip = g_got_ip_us - g_sta_connected_us;
  const int64_t app_to_ip = g_got_ip_us;

  ESP_LOGI(kTag, "SUMMARY app_to_sta_start_us=%" PRId64, app_to_start);
  ESP_LOGI(kTag, "SUMMARY sta_start_to_connected_us=%" PRId64, start_to_conn);
  ESP_LOGI(kTag, "SUMMARY connected_to_got_ip_us=%" PRId64, conn_to_ip);
  ESP_LOGI(kTag, "SUMMARY app_to_got_ip_us=%" PRId64, app_to_ip);
  ESP_LOGI(kTag, "SUMMARY disconnect_attempts=%d", g_disconnect_count);
  std::printf(
      "SUMMARY app_to_sta_start_us=%" PRId64
      " sta_start_to_connected_us=%" PRId64
      " connected_to_got_ip_us=%" PRId64 " app_to_got_ip_us=%" PRId64
      " disconnect_attempts=%d\n",
      app_to_start, start_to_conn, conn_to_ip, app_to_ip, g_disconnect_count);
  std::fflush(stdout);

  SendUdpBurst();

  ESP_LOGI(kTag, "idle forever");
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}
