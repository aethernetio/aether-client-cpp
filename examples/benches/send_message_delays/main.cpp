/*
 * Copyright 2024 Aethernet Inc.
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
#include <fstream>
#include <filesystem>

#include "aether/aether.h"
#include "aether/client.h"
#include "aether/global_ids.h"
#include "aether/obj/domain.h"
#include "aether/port/tele_init.h"
#include "aether/literal_array.h"

#include "aether/tele/tele.h"
#include "aether/tele/ios_time.h"

#if (defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
     defined(__FreeBSD__) || defined(_WIN64) || defined(_WIN32))
#  include "aether/port/file_systems/file_system_std.h"
#  include "aether/adapters/ethernet.h"
#elif (defined(ESP_PLATFORM))
#  include "aether/port/file_systems/file_system_ram.h"
#  include "aether/adapters/esp32_wifi.h"
#endif

#include "send_message_delays/receiver.h"
#include "send_message_delays/sender.h"
#include "send_message_delays/statistics_write.h"
#include "send_message_delays/send_message_delays_manager.h"

namespace ae::bench {

[[maybe_unused]] static constexpr char WIFI_SSID[] = "Test123";
[[maybe_unused]] static constexpr char WIFI_PASS[] = "Test123";

int test_send_message_delays(std::ostream& result_stream) {
  TeleInit::Init();
  AE_TELE_ENV();

  auto fs =
#if (defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
     defined(__FreeBSD__) || defined(_WIN64) || defined(_WIN32))
      ae::FileSystemStdFacility{};
#elif ((defined(ESP_PLATFORM)))
      ae::FileSystemRamFacility{};
#endif

#if AE_DISTILLATION
  {
    fs.remove_all();
    auto domain = Domain{TimePoint::clock::now(), fs};
    auto aether = domain.CreateObj<ae::Aether>(GlobalId::kAether);

#  if (defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
       defined(__FreeBSD__) || defined(_WIN64))
    EthernetAdapter::ptr adapter = domain.CreateObj<EthernetAdapter>(
        GlobalId::kEthernetAdapter, aether, aether->poller);
#  elif ((defined(ESP_PLATFORM)))
    Esp32WifiAdapter::ptr adapter = domain.CreateObj<Esp32WifiAdapter>(
        GlobalId::kEsp32WiFiAdapter, aether, aether->poller,
        std::string(WIFI_SSID), std::string(WIFI_PASS));
#  endif

    adapter.SetFlags(ae::ObjFlags::kUnloadedByDefault);
    aether->adapter_factories.emplace_back(std::move(adapter));

#  if AE_SIGNATURE == AE_ED25519
    aether->crypto->signs_pk_[ae::SignatureMethod::kEd25519] =
        ae::SodiumSignPublicKey{
            ae::MakeLiteralArray("4F202A94AB729FE9B381613AE77A8A7D89EDAB9299C33"
                                 "20D1A0B994BA710CCEB")};

#  elif AE_SIGNATURE == AE_HYDRO_SIGNATURE
    aether->crypto->signs_pk_[ae::SignatureMethod::kHydroSignature] =
        ae::HydrogenSignPublicKey{
            ae::MakeLiteralArray("883B4D7E0FB04A38CA12B3A451B00942048858263EE6E"
                                 "6D61150F2EF15F40343")};
#  endif  // AE_SIGNATURE == AE_ED25519

#  if AE_SUPPORT_REGISTRATION
    // localhost
    aether->registration_cloud->AddServerSettings(IpAddressPortProtocol{
        {IpAddress{IpAddress::Version::kIpV4, {{127, 0, 0, 1}}}, 9010},
        Protocol::kTcp});
    // cloud address
    aether->registration_cloud->AddServerSettings(IpAddressPortProtocol{
        {IpAddress{IpAddress::Version::kIpV4, {{35, 224, 1, 127}}}, 9010},
        Protocol::kTcp});
    // cloud name address
    aether->registration_cloud->AddServerSettings(
        NameAddress{"registration.aethernet.io", 9010, Protocol::kTcp});
#  endif  // AE_SUPPORT_REGISTRATION

    domain.SaveRoot(aether);
  }
#endif  // AE_DISTILLATION

  auto domain = Domain{TimePoint::clock::now(), fs};
  auto aether = Aether::ptr{};
  aether.SetId(GlobalId::kAether);
  domain.LoadRoot(aether);
  assert(aether);
  ae::TeleInit::Init(aether);

  ae::Adapter::ptr adapter{domain.LoadCopy(aether->adapter_factories.front())};

  ae::Client::ptr client_sender;
  ae::Client::ptr client_receiver;

  // register two clients
  if (aether->clients().size() != 2) {
#if AE_SUPPORT_REGISTRATION
    aether->clients().clear();

    auto& cloud = aether->registration_cloud;
    domain.LoadRoot(cloud);
    cloud->set_adapter(adapter);

    auto sender_register = aether->RegisterClient(
        ae::Uid{ae::MakeLiteralArray("3ac931653d37497087a6fa4ee27744e4")});
    auto receiver_register = aether->RegisterClient(
        ae::Uid{ae::MakeLiteralArray("3ac931653d37497087a6fa4ee27744e4")});

    bool sender_done = false;
    bool receiver_done = false;
    bool reg_failed = false;

    auto sender_reg = sender_register->SubscribeOnResult([&](auto const& reg) {
      sender_done = true;
      client_sender = reg.client();
    });
    auto sender_reg_failed = sender_register->SubscribeOnError(
        [&](auto const&) { reg_failed = true; });

    auto receiver_reg =
        receiver_register->SubscribeOnResult([&](auto const& reg) {
          receiver_done = true;
          client_receiver = reg.client();
        });
    auto receiver_reg_failed = receiver_register->SubscribeOnError(
        [&](auto const&) { reg_failed = true; });

    while (!(sender_done && receiver_done) && !reg_failed) {
      auto time = ae::TimePoint::clock::now();
      auto next_time = domain.Update(time);
      aether->action_processor->get_trigger().WaitUntil(next_time);

      if (reg_failed) {
        AE_TELED_ERROR("Registration failed");
        return -1;
      }
    }
#endif
  } else {
    client_sender = aether->clients()[0];
    client_receiver = aether->clients()[1];
  }

  // Set adapter for all clouds in the client to work though.
  client_sender->cloud()->set_adapter(adapter);
  client_receiver->cloud()->set_adapter(adapter);

  domain.SaveRoot(aether);

  SafeStreamConfig safe_stream_config;
  safe_stream_config.buffer_capacity =
      std::numeric_limits<std::uint16_t>::max();
  safe_stream_config.window_size = (safe_stream_config.buffer_capacity / 2) - 1;
  safe_stream_config.max_data_size = safe_stream_config.window_size;
  safe_stream_config.max_repeat_count = 5;
  safe_stream_config.send_repeat_timeout = std::chrono::milliseconds{300};
  safe_stream_config.send_confirm_timeout = std::chrono::milliseconds{0};
  safe_stream_config.wait_confirm_timeout = std::chrono::milliseconds{200};

  auto sender =
      MakePtr<Sender>(ActionContext{*aether->action_processor}, client_sender,
                      client_receiver->uid(), safe_stream_config);
  auto receiver = MakePtr<Receiver>(ActionContext{*aether->action_processor},
                                    client_receiver, safe_stream_config);

  auto send_message_delays_manager = MakePtr<SendMessageDelaysManager>(
      ActionContext{*aether->action_processor}, std::move(sender),
      std::move(receiver));

  auto test_action = send_message_delays_manager->Test({
      /* WarUp message count*/ 100,
      /* test message count */ 300,
      /* min send interval  */ std::chrono::milliseconds{50},
  });

  auto test_done = false;
  auto test_failed = false;

  auto result_subscription =
      test_action->SubscribeOnResult([&](auto const& action) {
        auto res_name_table = std::array{
            std::string_view{"p2p 2 Bytes"},
            std::string_view{"p2p 10 Bytes"},
            std::string_view{"p2p 100 Bytes"},
            std::string_view{"p2p 1000 Bytes"},
            std::string_view{"p2p 1500 Bytes"},
            std::string_view{"p2pss 2 Bytes"},
            std::string_view{"p2pss 10 Bytes"},
            std::string_view{"p2pss 100 Bytes"},
            std::string_view{"p2pss 1000 Bytes"},
            std::string_view{"p2pss 1500 Bytes"},
        };
        auto const& results = action.result_table();

        std::vector<std::pair<std::string, DurationStatistics>>
            statistics_write_list;
        statistics_write_list.reserve(res_name_table.size());
        for (std::size_t i = 0; i < res_name_table.size(); ++i) {
          statistics_write_list.emplace_back(std::string{res_name_table[i]},
                                             std::move(results[i]));
        }
        Format(result_stream, "{}",
               StatisticsWriteCsv{std::move(statistics_write_list)});

        test_done = true;
      });

  auto error_subscription = test_action->SubscribeOnError([&](auto const&) {
    AE_TELED_ERROR("Test failed");
    test_failed = true;
  });

  while (!(test_done || test_failed)) {
    auto time = ae::TimePoint::clock::now();
    auto next_time = domain.Update(time);
    aether->action_processor->get_trigger().WaitUntil(
        std::min(next_time, time + std::chrono::seconds(5)));
  }

  return 0;
}
}  // namespace ae::bench

#if defined ESP_PLATFORM
extern "C" void app_main();

void app_main(void) { ae::bench::test_send_message_delays(std::cout); }
#endif

#if (defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
     defined(__FreeBSD__) || defined(_WIN64) || defined(_WIN32))
int main(int argc, char* argv[]) {
  if (argc < 2) {
    return ae::bench::test_send_message_delays(std::cout);
  }

  auto file = std::filesystem::path{argv[1]};
  std::cout << "Save results to " << file << std::endl;
  auto file_stream = std::fstream{file, std::ios::binary | std::ios::out};
  if (!file_stream.is_open()) {
    std::cerr << "Failed to open file " << file << std::endl;
    return -1;
  }
  return ae::bench::test_send_message_delays(file_stream);
}
#endif
