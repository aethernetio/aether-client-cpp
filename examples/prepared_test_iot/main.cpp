// Copyright 2026 Aethernet Inc.
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#include "aether-miscpp/format/format.h"
#include "aether-objects/domain_storage/ram_domain_storage.h"
#include "aether-objects/obj/obj_ptr.h"
#include "aether/adapter_registry.h"
#include "aether/adapters/modem_adapter.h"
#include "aether/aether_app.h"
#include "aether/connection_manager/client_cloud_manager.h"
#include "aether/executors/executors.h"
#include "aether/global_ids.h"
#include "aether/modems/imodem_driver.h"
#include "aether/modems/modem_factory.h"
#include "aether/prepared_packet/packet_encoder.h"
#include "aether/types/uid.h"

#if !AE_SUPPORT_MODEMS || AE_ENABLE_BG95 || \
    (AE_ENABLE_SIM7070 + AE_ENABLE_THINGY91X != 1)
#  error "Enable exactly one modem: SIM7070 or Thingy91X"
#endif
#if !AE_SUPPORT_UDP || !AE_SUPPORT_REGISTRATION
#  error "Prepared IoT example requires UDP and registration support"
#endif

namespace ae::examples::main_internal {
using Block = prepared_packet::PreparedSendMessageBlock;
constexpr auto kOperationTimeout = std::chrono::seconds{180};
constexpr auto kPreparationTimeout = std::chrono::minutes{10};

struct Options {
  ModemInit modem{};
  Uid destination{};
  Uid parent = Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4");
  int temperature = 2500;
};

void Usage() {
  std::cout
      << "prepared-test-iot COM APN DESTINATION_UID [TEMPERATURE_CENTI_C] "
         "[nb-iot|lte-m] [OPERATOR_CODE] [PARENT_UID]\n"
#if AE_ENABLE_SIM7070
      << "SIM7070"
#else
      << "Thingy:91 X (Serial LTE Modem firmware)"
#endif
      << ", 115200 baud, SIM PIN disabled. Default: 2500 (25 C), "
         "NB-IoT, automatic operator selection.\n"
      << "Registers/selects a sender over LTE, prepares one packet, shuts down "
         "the full client, then sends it through a fresh modem UDP socket.\n";
}

bool ParseUid(std::string_view text, Uid& uid) {
  if (text.size() != 36) {
    return false;
  }
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (text[i] != '-') {
        return false;
      }
    } else if (!((text[i] >= '0' && text[i] <= '9') ||
                 (text[i] >= 'a' && text[i] <= 'f') ||
                 (text[i] >= 'A' && text[i] <= 'F'))) {
      return false;
    }
  }
  uid = Uid::FromString(UidString{text});
  return uid != Uid{};
}

bool ParseOptions(int argc, char** argv, Options& options) {
  if (argc < 4 || argc > 8 || !ParseUid(argv[3], options.destination)) {
    return false;
  }
  auto port = std::string{argv[1]};
  if (!port.starts_with("COM") || port.size() <= 3 ||
      !std::all_of(port.begin() + 3, port.end(),
                   [](char c) { return c >= '0' && c <= '9'; })) {
    return false;
  }
  // WinSerialPort adds the Windows device namespace (including COM10+).
  options.modem.serial_init = {port, kBaudRate::kBaudRate115200};
  auto apn = std::string{argv[2]};
  if (apn.empty() || !std::all_of(apn.begin(), apn.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '.' || c == '-';
      })) {
    return false;
  }
  options.modem.apn_name = std::move(apn);
  options.modem.modem_mode = kModemMode::kModeNbIot;
  if (argc >= 5) {
    auto text = std::string_view{argv[4]};
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(),
                                        options.temperature);
    if (error != std::errc{} || end != text.data() + text.size() ||
        options.temperature < -3000 || options.temperature > 12500) {
      return false;
    }
  }
  if (argc >= 6) {
    auto mode = std::string_view{argv[5]};
    if (mode == "lte-m") {
      options.modem.modem_mode = kModemMode::kModeCatM;
    } else if (mode != "nb-iot") {
      return false;
    }
  }
  if (argc >= 7) {
    auto code = std::string{argv[6]};
    if (!code.empty() && ((code.size() != 5 && code.size() != 6) ||
                          !std::all_of(code.begin(), code.end(), [](char c) {
                            return c >= '0' && c <= '9';
                          }))) {
      return false;
    }
    options.modem.operator_code = std::move(code);
  }
  return argc < 8 || ParseUid(argv[7], options.parent);
}

// Drive the application scheduler, retaining the waiter until completion.
template <typename Sender, typename Duration>
bool Await(AetherApp& app, Sender&& sender, Duration timeout,
           std::string_view label) {
  bool success = false;
  bool done = false;
  auto pipeline = std::forward<Sender>(sender) |
                  ex::with_timeout(AeContext{app}, timeout) |
                  ex::then([&](auto&&...) noexcept { success = true; }) |
                  ex::upon_error([&](auto const& error) noexcept {
                    if constexpr (std::is_same_v<std::decay_t<decltype(error)>,
                                                 ex::TimeoutError>) {
                      std::cerr << label << ": timeout\n";
                    } else {
                      std::cerr << label << ": operation failed\n";
                    }
                  });
  auto waiter = ex::AsyncWaiter{AeContext{app}, std::move(pipeline),
                                [&](auto) { done = true; }};
  while (!done) {
    auto next = app.Update(Now());
    if (!done) {
      app.WaitUntil(next);
    }
  }
  return success;
}

template <typename Operation, typename Consumer>
bool AwaitModem(AetherApp& app, Operation* operation, Consumer&& consume,
                std::string_view label) {
  if (operation == nullptr) {
    std::cerr << label << ": modem rejected operation\n";
    return false;
  }
  // Drivers can return an already-completed action; action_wait only observes
  // future events, so inspect the stored result before subscribing.
  if (operation->result()) {
    auto const& result = *operation->result();
    if (!result) {
      std::cerr << label << ": modem error " << static_cast<int>(result.error())
                << '\n';
      return false;
    }
    consume(result.value());
    return true;
  }
  return Await(
      app,
      ex::action_wait(*operation) | ex::then(std::forward<Consumer>(consume)),
      kOperationTimeout, label);
}

std::optional<Block> Prepare(Options const& options) {
  auto app = AetherApp::Construct(
      AetherAppContext{}.AddAdapterFactory([&](AetherAppContext const& ctx) {
        return ModemAdapter::ptr::Create(
            CreateWith{ctx.domain()}.with_id(GlobalId::kModemAdapter),
            ctx.aether(), ctx.poller(), options.modem);
      }));
  if (!app) {
    std::cerr << "Cannot construct preparation application\n";
    return std::nullopt;
  }
  Client::ptr client;
  auto pipeline = ex::action_wait(app->aether()->SelectClient(
                      options.parent, "prepared-test-iot")) |
                  ex::then([&](Client::ptr const& selected) noexcept {
                    client = selected;
                  });
  auto ready = Await(*app, std::move(pipeline), kPreparationTimeout,
                     "Sender registration");
  if (ready) {
    auto loaded_client = client.Load();
    if (!loaded_client) {
      ready = false;
    } else {
      auto manager = loaded_client->cloud_manager().Load();
      ready =
          manager &&
          Await(*app, ex::action_wait(manager->GetCloud(options.destination)),
                kPreparationTimeout, "Destination cloud lookup");
    }
  }

  // Stop ordinary traffic and close the serial port before reserving the nonce.
  // Port release must not depend on reclamation of the persistent object graph.
  app->Exit(ready ? 0 : 1);
  while (!app->IsExited()) {
    app->WaitUntil(app->Update(Now()));
  }
  if (!ready || app->ExitCode() != 0) {
    return std::nullopt;
  }
  auto prepared =
      prepared_packet::PrepareSendMessageBlock(client, options.destination, 1);
  if (!prepared) {
    std::cerr << "Preparation failed: " << prepared.error().msg << '\n';
    return std::nullopt;
  }
  std::cout << "Prepared one message; modem stopped and serial port released\n";
  // AetherApp's destructor persists the reserved nonce range. The prepared
  // block itself lives only in this process and is never replayed from disk.
  return std::move(prepared).value();
}

bool Send(Options const& options, Block& block) {
  // A separate RAM-only domain supplies the scheduler and Windows poller.
  // It has no clients or adapters and cannot send ordinary Aether messages.
  auto context =
      AetherAppContext{[] { return std::make_unique<RamDomainStorage>(); }};
  auto app = AetherApp::Construct(
      std::move(context).AdaptersFactory([](AetherAppContext const& ctx) {
        return AdapterRegistry::ptr::Create(
            CreateWith{ctx.domain()}.with_id(GlobalId::kAdapterRegistry));
      }));
  if (!app) {
    return false;
  }
  auto const& poller = app->aether()->poller;
  auto loaded_poller = poller.Load();
  if (!loaded_poller) {
    return false;
  }
  DataBuffer packet;
  auto modem = ModemDriverFactory::CreateModem(*app, poller, options.modem);
  if (!modem) {
    return false;
  }
  auto ignore = [](auto const&) noexcept {};
  auto success = AwaitModem(*app, modem->Start(), ignore, "LTE registration");
  auto endpoint = block.Resolve()->endpoint;
  ConnectionIndex connection = kInvalidConnectionIndex;
  if (success && (endpoint.protocol != Protocol::kUdp ||
                  endpoint.address.Index() != AddrVersion::kIpV4)) {
    std::cerr << "This example requires an IPv4 UDP endpoint\n";
    success = false;
  }
  if (success) {
    auto host = Format("{}", endpoint.address.Get<IpV4Addr>());
    std::cout << "Opening LTE UDP socket to " << host << ':' << endpoint.port
              << '\n';
    success = AwaitModem(
        *app, modem->OpenNetwork(Protocol::kUdp, host, endpoint.port),
        [&](ConnectionIndex value) noexcept { connection = value; },
        "UDP open");
  }
  if (success) {
    // Exact thermometer payload: API prefix followed by encoded temperature.
    auto temperature =
        static_cast<std::uint16_t>((options.temperature / 100 + 30) * 3);
    auto payload = DataBuffer{0x03, 0x03, 0x0A,
                              static_cast<std::uint8_t>(temperature & 0xff),
                              static_cast<std::uint8_t>(temperature >> 8)};
    auto encoded = prepared_packet::EncodePacket(block, payload, packet);
    success = static_cast<bool>(encoded);
    if (!success) {
      std::cerr << "EncodePacket failed: " << encoded.error().msg << '\n';
    }
  }
  if (success) {
    std::size_t written = 0;
    success = AwaitModem(
        *app, modem->WritePacket(connection, packet),
        [&](std::size_t size) noexcept { written = size; }, "UDP send");
    success = success && written == packet.size();
    if (success) {
      std::cout
          << "Modem accepted " << written
          << " prepared UDP bytes (recipient delivery is not confirmed)\n";
    } else {
      std::cerr << "Prepared UDP packet was not fully sent\n";
    }
  }
  // Stop also closes sockets. Keep packet, driver, poller and scheduler alive
  // while any pending write and the queued shutdown operation complete.
  auto* stop = modem->Stop();
  auto stopped = AwaitModem(*app, stop, ignore, "Modem shutdown");
  // A waiter timeout does not cancel driver-owned actions. Drain shutdown
  // before releasing the driver; every AT stage has its own bounded wait.
  if (stop != nullptr) {
    while (!stop->is_finished()) {
      auto next = app->Update(Now());
      if (!stop->is_finished()) {
        app->WaitUntil(next);
      }
    }
  }
  return success && stopped;
}
}  // namespace ae::examples::main_internal

int main(int argc, char** argv) {
  if (argc == 2 && std::string_view{argv[1]} == "--help") {
    ae::examples::main_internal::Usage();
    return 0;
  }
  ae::examples::main_internal::Options options;
  if (!ae::examples::main_internal::ParseOptions(argc, argv, options)) {
    ae::examples::main_internal::Usage();
    return 2;
  }
  auto block = ae::examples::main_internal::Prepare(options);
  if (!block) {
    return 1;
  }
  return ae::examples::main_internal::Send(options, *block) ? 0 : 1;
}
