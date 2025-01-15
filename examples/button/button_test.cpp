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

#include "aether/aether.h"
#include "aether/common.h"
#include "aether/global_ids.h"
#include "aether/port/tele_init.h"
#include "aether/literal_array.h"
#include "aether/crypto/sign.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/ae_actions/registration/registration.h"

#if (defined(__linux__) || defined(__unix__) || defined(__FreeBSD__) || \
     defined(_WIN64) || defined(_WIN32) || defined(__APPLE__))
#  include "aether/port/file_systems/file_system_header.h"
#  include "aether/port/file_systems/file_system_std.h"
#  include "aether/adapters/ethernet.h"
#  include "aether/adapters/register_wifi.h"
#elif (defined(ESP_PLATFORM))
#  include "aether/port/file_systems/file_system_header.h"
#  include "aether/port/file_systems/file_system_ram.h"
#  include "aether/port/file_systems/file_system_spifs_v1.h"
#  include "aether/port/file_systems/file_system_spifs_v2.h"
#  include "aether/adapters/esp32_wifi.h"
#endif

#include "led_button_nix.h"
#include "led_button_win.h"
#include "led_button_mac.h"
#include "led_button_esp.h"

#include "aether/tele/tele.h"

using std::vector;

static constexpr char WIFI_SSID[] = "Test123";
static constexpr char WIFI_PASS[] = "Test123";
static constexpr bool use_aether = true;
static constexpr int wait_time = 100;

namespace ae {
#if defined AE_DISTILLATION
static Aether::ptr CreateAetherInstrument(Domain& domain) {
  Aether::ptr aether = domain.CreateObj<ae::Aether>(GlobalId::kAether);

#  if (defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
       defined(__FreeBSD__) || defined(_WIN64) || defined(_WIN32))
  EthernetAdapter::ptr adapter = domain.CreateObj<EthernetAdapter>(
      GlobalId::kEthernetAdapter, aether, aether->poller);
  adapter.SetFlags(ae::ObjFlags::kUnloadedByDefault);
#  elif ((defined(ESP_PLATFORM)))
  Esp32WifiAdapter::ptr adapter = domain.CreateObj<Esp32WifiAdapter>(
      GlobalId::kEsp32WiFiAdapter, aether, aether->poller,
      std::string(WIFI_SSID), std::string(WIFI_PASS));
  adapter.SetFlags(ae::ObjFlags::kUnloadedByDefault);
#  endif

  aether->adapter_factories.emplace_back(std::move(adapter));

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

  return aether;
}
#endif

static Aether::ptr LoadAether(Domain& domain) {
  Aether::ptr aether;
  aether.SetId(GlobalId::kAether);
  domain.LoadRoot(aether);
  assert(aether);
  return aether;
}

}  // namespace ae

void AetherButtonExample();

void AetherButtonExample() {
  ae::TeleInit::Init();
  {
    AE_TELE_ENV();
    AE_TELE_INFO("Started");
    ae::Registry::Log();
  }

  auto fs =
#if (defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
     defined(__FreeBSD__) || defined(_WIN64) || defined(_WIN32))
      ae::FileSystemStdFacility{};
#elif (defined(ESP_PLATFORM))
#  if defined AE_DISTILLATION
      ae::FileSystemRamFacility{};
#  else
      ae::FileSystemHeaderFacility{};
#  endif
#endif

#ifdef AE_DISTILLATION
  // create objects in instrument mode
  {
    ae::Domain domain{ae::ClockType::now(), fs};
    fs.remove_all();
    ae::Aether::ptr aether = ae::CreateAetherInstrument(domain);
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
    domain.SaveRoot(aether);
  }
#endif  // AE_DISTILLATION

  ae::Domain domain{ae::ClockType::now(), fs};
  ae::Aether::ptr aether = ae::LoadAether(domain);
  ae::TeleInit::Init(aether);

  ae::Adapter::ptr adapter{domain.LoadCopy(aether->adapter_factories.front())};

  adapter->Update(ae::ClockType::now());

  ae::Client::ptr client_sender;
  ae::Client::ptr client_receiver;

  //  Create ad client and perform registration
  if (aether->clients().size() < 2) {
#if AE_SUPPORT_REGISTRATION
    // Creating the actual adapter.
    auto& cloud = aether->registration_cloud;
    domain.LoadRoot(cloud);
    cloud->set_adapter(adapter);

    auto registration1 = aether->RegisterClient(
        ae::Uid{ae::MakeLiteralArray("3ac931653d37497087a6fa4ee27744e4")});
    auto registration2 = aether->RegisterClient(
        ae::Uid{ae::MakeLiteralArray("3ac931653d37497087a6fa4ee27744e4")});

    bool reg_failed = false;

    auto sender_reg = registration1->SubscribeOnResult(
        [&](auto const& action) { client_sender = action.client(); });
    auto sender_reg_failed = registration1->SubscribeOnError(
        [&](auto const&) { reg_failed = true; });

    auto receiver_reg = registration2->SubscribeOnResult(
        [&](auto const& action) { client_receiver = action.client(); });
    auto receiver_reg_failed = registration2->SubscribeOnError(
        [&](auto const&) { reg_failed = true; });

    while ((!client_sender && !client_receiver) && !reg_failed) {
      auto now = ae::Now();
      auto next_time = aether->domain_->Update(now);
      aether->action_processor->get_trigger().WaitUntil(
          std::min(next_time, now + std::chrono::milliseconds{wait_time}));
    }
    if (reg_failed) {
      AE_TELED_ERROR("Registration rejected");
      return;
    }
#else
    assert(false);
#endif
  } else {
    client_sender = aether->clients()[0];
    client_receiver = aether->clients()[1];
    aether->domain_->LoadRoot(client_sender);
    aether->domain_->LoadRoot(client_receiver);
  }

  assert(client_sender);
  assert(client_receiver);

  // Set adapter for all clouds in the client to work though.
  client_sender->cloud()->set_adapter(adapter);
  client_receiver->cloud()->set_adapter(adapter);

  std::vector<std::string> messages = {"LED on", "LED off"};

#if defined BUTTON_NIX
  auto button_action =
      ae::LedButtonNix{ae::ActionContext{*aether->action_processor}};
#elif defined BUTTON_WIN
  auto button_action =
      ae::LedButtonWin{ae::ActionContext{*aether->action_processor}};
#elif defined BUTTON_MAC
  auto button_action =
      ae::LedButtonMac{ae::ActionContext{*aether->action_processor}};
#elif defined BUTTON_ESP
  auto button_action =
      ae::LedButtonEsp{ae::ActionContext{*aether->action_processor}};
#endif

  auto receiver_connection = client_receiver->client_connection();

  ae::Subscription receiver_subscription;
  ae::Ptr<ae::ByteStream> receiver_stream;
  auto _s0 = receiver_connection->new_stream_event().Subscribe(
      [&](auto uid, auto stream_id, auto const& stream) {
        AE_TELED_DEBUG("Received stream from {}", uid);
        receiver_stream = ae::MakePtr<ae::P2pStream>(
            *aether->action_processor, client_receiver, uid, stream_id,
            std::move(stream));
        receiver_subscription =
            receiver_stream->in().out_data_event().Subscribe(
                [&](auto const& data) {
                  auto str_msg = std::string(
                      reinterpret_cast<const char*>(data.data()), data.size());
                  AE_TELED_DEBUG("Received a message {}", str_msg);
                  if ((str_msg.compare(messages[0])) == 0) {
                    button_action.SetLed(1);
                    AE_TELED_INFO("LED is on");
                  } else if ((str_msg.compare(messages[1])) == 0) {
                    button_action.SetLed(0);
                    AE_TELED_INFO("LED is off");
                  }
                });
      });

  auto sender_to_receiver_stream =
      ae::MakePtr<ae::P2pStream>(*aether->action_processor, client_sender,
                                 client_receiver->uid(), ae::StreamId{0});

  auto _bas = button_action.SubscribeOnResult(
      [&, sender_to_receiver_stream{std::move(sender_to_receiver_stream)}](
          auto const&) {
        if (button_action.GetKey()) {
          AE_TELED_INFO("Hi level Press");
          if (use_aether) {
            sender_to_receiver_stream->in().WriteIn(
                {std::begin(messages[0]), std::end(messages[0])},
                ae::TimePoint::clock::now());
          } else {
            button_action.SetLed(1);
          }
        } else {
          AE_TELED_INFO("Low level Press");
          if (use_aether) {
            sender_to_receiver_stream->in().WriteIn(
                {std::begin(messages[1]), std::end(messages[1])},
                ae::TimePoint::clock::now());
          } else {
            button_action.SetLed(0);
          }
        }
      });

  while (1) {
    auto now = ae::Now();
    auto next_time = aether->domain_->Update(now);
    aether->action_processor->get_trigger().WaitUntil(
        std::min(next_time, now + std::chrono::milliseconds(wait_time)));
#if ((defined(ESP_PLATFORM)))
    auto heap_free = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    AE_TELED_INFO("Heap size {}", heap_free);
#endif
  }

  aether->domain_->Update(ae::ClockType::now());

  // save objects state
  domain.SaveRoot(aether);
}
