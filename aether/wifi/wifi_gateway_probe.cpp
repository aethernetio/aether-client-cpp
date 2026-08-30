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

#include "aether/wifi/wifi_gateway_probe.h"

#if defined(ESP_PLATFORM)

#  include <freertos/FreeRTOS.h>
#  include <freertos/semphr.h>
#  include <lwip/inet.h>
#  include <lwip/ip_addr.h>
#  include <ping/ping_sock.h>

namespace ae {
namespace wifi_gateway_probe_internal {

struct PingCtx {
  std::uint16_t received{0};
  SemaphoreHandle_t done{nullptr};
};

void OnSuccess(esp_ping_handle_t, void* args) {
  auto* ctx = static_cast<PingCtx*>(args);
  ++ctx->received;
}

void OnTimeout(esp_ping_handle_t, void*) {}

void OnEnd(esp_ping_handle_t handle, void* args) {
  auto* ctx = static_cast<PingCtx*>(args);
  if (ctx->done != nullptr) {
    xSemaphoreGive(ctx->done);
  }
  esp_ping_delete_session(handle);
}

}  // namespace wifi_gateway_probe_internal

WifiGatewayProbeResult WifiGatewayIcmpProbe(std::uint32_t gateway_be,
                                            std::uint16_t count) {
  using namespace wifi_gateway_probe_internal;
  WifiGatewayProbeResult out;
  if (gateway_be == 0 || count == 0) {
    out.icmp_supported = false;
    return out;
  }

  PingCtx ctx{};
  ctx.done = xSemaphoreCreateBinary();
  if (ctx.done == nullptr) {
    out.icmp_supported = false;
    return out;
  }

  ip_addr_t target{};
  target.type = IPADDR_TYPE_V4;
  target.u_addr.ip4.addr = gateway_be;

  esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
  cfg.target_addr = target;
  cfg.count = count;
  cfg.interval_ms = 200;
  cfg.timeout_ms = 1000;
  cfg.task_stack_size = 4096;

  esp_ping_callbacks_t cbs{};
  cbs.cb_args = &ctx;
  cbs.on_ping_success = OnSuccess;
  cbs.on_ping_timeout = OnTimeout;
  cbs.on_ping_end = OnEnd;

  esp_ping_handle_t ping = nullptr;
  if (esp_ping_new_session(&cfg, &cbs, &ping) != ESP_OK || ping == nullptr) {
    vSemaphoreDelete(ctx.done);
    out.icmp_supported = false;
    return out;
  }
  if (esp_ping_start(ping) != ESP_OK) {
    esp_ping_delete_session(ping);
    vSemaphoreDelete(ctx.done);
    out.icmp_supported = false;
    return out;
  }

  auto const wait_ticks =
      pdMS_TO_TICKS(static_cast<int>(count) * (cfg.interval_ms + cfg.timeout_ms) +
                    2000);
  (void)xSemaphoreTake(ctx.done, wait_ticks);
  vSemaphoreDelete(ctx.done);

  out.stats.sent = count;
  out.stats.received = ctx.received;
  out.network_ready = true;
  if (out.stats.received == 0) {
    out.icmp_supported = false;
  }
  return out;
}

}  // namespace ae

#endif  // ESP_PLATFORM
