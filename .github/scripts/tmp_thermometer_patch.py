from __future__ import annotations

import sys
from pathlib import Path


if len(sys.argv) != 2:
    raise SystemExit("usage: tmp_thermometer_patch.py <project_path>")

project_path = Path(sys.argv[1])

Path("config/user_config_thermometer.h").write_text(
    r'''#ifndef CONFIG_USER_CONFIG_THERMOMETER_H_
#define CONFIG_USER_CONFIG_THERMOMETER_H_
#include "aether/config_consts.h"
#define AE_CRYPTO_ASYNC AE_HYDRO_CRYPTO_PK
#define AE_CRYPTO_SYNC AE_HYDRO_CRYPTO_SK
#define AE_KDF AE_HYDRO_KDF
#define AE_CRYPTO_HASH AE_HYDRO_HASH
#define AE_SUPPORT_IPV4 1
#define AE_SUPPORT_IPV6 0
#define AE_SUPPORT_UDP 1
#define AE_UDP_PACKET_QUEUE_SIZE 2
#define AE_SUPPORT_TCP 0
#define AE_SUPPORT_WEBSOCKET 0
#define AE_SUPPORT_HTTP 0
#define AE_SUPPORT_HTTP_OVER_TCP 0
#define AE_SUPPORT_HTTP_WINHTTP 0
#define AE_SUPPORT_HTTPS 0
#define AE_SUPPORT_PROXY 0
#define AE_SUPPORT_DYNAMIC_PROXY 0
#define AE_SUPPORT_REGISTRATION 0
#define AE_SUPPORT_CLOUD_DNS 0
#define AE_SUPPORT_DYNAMIC_CLOUD_DNS 0
#define AE_SUPPORT_CLOUD_IPS 1
#define AE_SUPPORT_DYNAMIC_CLOUD_IPS 0
#define AE_SUPPORT_WIFIS 1
#define AE_ENABLE_ESP32_WIFI 1
#define AE_SUPPORT_MODEMS 0
#define AE_SUPPORT_LORA 0
#define AE_SUPPORT_GATEWAY 0
#define AE_SUPPORT_SPIFS_FS 0
#define AE_TELE_ENABLED 0
#define AE_TELE_COMPILATION_INFO 0
#define AE_TELE_RUNTIME_INFO AE_NONE
#define AE_TELE_LOG_CONSOLE 0
#define AE_TELE_LOG_TO_STATISTICS 0
#define AE_CLOUD_MAX_SERVER_CONNECTIONS 1
#define AE_ENABLE_PING 0
#define AE_TASK_MAX_COUNT 24
#define AE_API_PROTOCOL_MAX_PENDING_RESPONSES 2
#define AE_STATISTICS_CONNECTION_WINDOW_SIZE 8
#define AE_STATISTICS_RESPONSE_WINDOW_SIZE 1
#define AE_STATISTICS_SAFE_STREAM_WINDOW_SIZE 1
#endif
'''
)

with (project_path / "sdkconfig.defaults").open("a") as f:
    f.write(
        r'''

# Temporary thermometer footprint profile
CONFIG_COMPILER_OPTIMIZATION_SIZE=y
CONFIG_COMPILER_OPTIMIZATION_ASSERTIONS_DISABLE=y
CONFIG_COMPILER_CXX_EXCEPTIONS=n
CONFIG_COMPILER_CXX_RTTI=n
CONFIG_LOG_DEFAULT_LEVEL_NONE=y
CONFIG_BOOTLOADER_LOG_LEVEL_NONE=y
CONFIG_LWIP_IPV6=n
CONFIG_LWIP_STATS=n
CONFIG_ESP_WIFI_ENABLE_WIFI_RX_STATS=n
CONFIG_ESP_WIFI_ENABLE_WIFI_TX_STATS=n
CONFIG_ESP_WIFI_NVS_ENABLED=n
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_LWIP_TCPIP_TASK_STACK_SIZE=4096
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=4
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=8
CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER_NUM=8
CONFIG_LWIP_UDP_RECVMBOX_SIZE=8
CONFIG_LWIP_TCPIP_RECVMBOX_SIZE=8
'''
    )

Path("examples/cloud/cloud_test.cpp").write_text(
    r'''#include <chrono>
#include <cstdint>
#include <memory>

#include "aether/all.h"
#include "aether/client_messages/p2p_message_stream.h"

#define AE_EXAMPLE_LORA_MODULE 0
#define AE_EXAMPLE_MODEM 0
#if defined ESP_PLATFORM
#  define AE_EXAMPLE_ESP_WIFI 1
#else
#  define AE_EXAMPLE_ETHERNET 1
#endif
#include "../common/aether_construct_esp_wifi.h"
#include "../common/aether_construct_ethernet.h"
#include "../common/aether_construct_lora_module.h"
#include "../common/aether_construct_modem.h"

namespace ae::examples {
static constexpr inline auto kParentUid =
    ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4");
static constexpr inline auto kDestinationUid =
    ae::Uid::FromString("a0010203-0405-4607-8809-0a0b0c0d0e0f");
}

int AetherCloudExample() {
  auto app = ae::examples::construct_aether_app();
  ae::Client::ptr client;
  auto& select =
      app->aether()->SelectClient(ae::examples::kParentUid, "thermometer");
  select.result_event().Subscribe([&](auto&& result) {
    if (result) {
      client = std::move(result).value();
    } else {
      app->Exit(1);
    }
  });
  app->WaitActions(select);
  assert(client);

  auto handle = client->message_stream_manager().CreatePort(
      ae::examples::kDestinationUid);
  auto stream = std::make_shared<ae::P2pStream>(
      *app, client.Load(), ae::examples::kDestinationUid, std::move(handle));

  ae::DataBuffer data{1, 2, 3, 4, 5, 6, 7, 8};
  auto& action = stream->Write(std::move(data));
  action.status_event().Subscribe([&](auto status) {
    app->Exit(status == ae::WriteAction::Status::kSuccess ? 0 : 1);
  });

  while (!app->IsExited()) {
    auto now = ae::Now();
    auto next = app->Update(now);
    app->WaitUntil(std::min(next, now + std::chrono::seconds{5}));
  }
  return app->ExitCode();
}
'''
)

# Remove unconditional iostream initialization and fix the telemetry namespace.
path = Path("aether/aether_app.cpp")
text = path.read_text().replace("#include <iostream>\n", "", 1)
old = "return tele::TeleStatistics::ptr{};"
assert text.count(old) == 1
path.write_text(text.replace(old, "return TeleStatistics::ptr{};", 1))

path = Path("examples/cloud/main.cpp")
text = path.read_text().replace('#  include <iostream>\n', '')
old = '''  esp_err_t err = esp_task_wdt_reconfigure(&config_wdt);
  if (err != 0) {
    std::cerr << "Reconfigure WDT is failed!\\n";
  }
'''
assert text.count(old) == 1
path.write_text(
    text.replace(
        old,
        '''  esp_err_t err = esp_task_wdt_reconfigure(&config_wdt);
  (void)err;
''',
        1,
    )
)

# SetConfig creates objects whose non-default constructors only exist in
# distillation mode. A production image loads these objects from saved state.
path = Path("aether/client.cpp")
text = path.read_text()
begin = "  connectivity_policy_ = ClientConnectivityPolicy::ptr::Create("
end = "      Aether::ptr{aether_}, Client::ptr::MakeFromThis(this));"
assert text.count(begin) == 1 and text.count(end) == 1
text = text.replace(begin, "#if AE_DISTILLATION\n" + begin, 1)
path.write_text(text.replace(end, end + "\n#endif", 1))

# DNS-off builds otherwise fail -Werror due to unused named parameters.
for filename, old_sig, new_sig in [
    (
        "aether/channels/wifi_channel.cpp",
        "ResolveSender ResolveAddress(PtrView<DnsResolver> const& resolver,\n"
        "                             NamedAddr const& addr, std::uint16_t port,\n"
        "                             Protocol protocol)",
        "ResolveSender ResolveAddress([[maybe_unused]] PtrView<DnsResolver> const& resolver,\n"
        "                             [[maybe_unused]] NamedAddr const& addr,\n"
        "                             [[maybe_unused]] std::uint16_t port,\n"
        "                             [[maybe_unused]] Protocol protocol)",
    ),
    (
        "aether/channels/ethernet_channel.cpp",
        "ResolveSender ResolveAddress(Ptr<DnsResolver> const& resolver,\n"
        "                             NamedAddr const& addr, std::uint16_t port,\n"
        "                             Protocol protocol)",
        "ResolveSender ResolveAddress([[maybe_unused]] Ptr<DnsResolver> const& resolver,\n"
        "                             [[maybe_unused]] NamedAddr const& addr,\n"
        "                             [[maybe_unused]] std::uint16_t port,\n"
        "                             [[maybe_unused]] Protocol protocol)",
    ),
]:
    p = Path(filename)
    t = p.read_text()
    assert t.count(old_sig) == 1
    p.write_text(t.replace(old_sig, new_sig, 1))

# Make RAM the explicit writable storage.
path = Path("aether/domain_storage/ram_domain_storage.h")
text = path.read_text()
assert text.count("#define AE_FILE_SYSTEM_RAM_ENABLED 0") == 1
path.write_text(
    text.replace(
        "#define AE_FILE_SYSTEM_RAM_ENABLED 0",
        "#define AE_FILE_SYSTEM_RAM_ENABLED 1",
        1,
    )
)

# Lower bound for a generated static registry: remove all per-class global
# Registrar objects. A real generated profile will add only required entries.
path = Path("aether/obj/obj.h")
lines = path.read_text().splitlines(keepends=True)
out: list[str] = []
removed = 0
i = 0
while i < len(lines):
    if "inline static auto registrar_" in lines[i]:
        out.append(
            "  inline static constexpr bool registrar_disabled_ = true;                \\\n"
        )
        i += 1
        if i < len(lines) and "Registrar<DERIVED>" in lines[i]:
            i += 1
        removed += 1
        continue
    out.append(lines[i])
    i += 1
assert removed == 1
path.write_text("".join(out))

# Avoid std::random_device and mt19937.
Path("aether/obj/obj_id.cpp").write_text(
    r'''#include "aether/obj/obj_id.h"
#include <hydrogen.h>
namespace ae {
ObjId ObjId::GenerateUnique() {
  ObjId::Type value;
  do { value = static_cast<ObjId::Type>(hydro_random_u32()); }
  while (value < 10000);
  return ObjId{value};
}
}
'''
)

# Compile Hydrogen itself for size and allow per-function dead stripping.
path = Path("CMakeLists.txt")
text = path.read_text()
marker = '''CPMAddPackage(
  NAME libsodium'''
insert = '''target_compile_options(hydrogen PRIVATE -Os -ffunction-sections -fdata-sections -flto=auto)
set_property(TARGET hydrogen PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)

CPMAddPackage(
  NAME libsodium'''
assert text.count(marker) == 1
path.write_text(text.replace(marker, insert, 1))
