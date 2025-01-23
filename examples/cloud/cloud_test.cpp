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

#include <thread>

#include "aether/aether.h"
#include "aether/ae_actions/registration/registration.h"
#include "aether/common.h"
#include "aether/global_ids.h"
#include "aether/port/tele_init.h"
#include "aether/literal_array.h"
#include "aether/crypto/sign.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/client_messages/p2p_safe_message_stream.h"

#if (defined(__linux__) || defined(__unix__) || defined(__APPLE__) || \
     defined(__FreeBSD__) || defined(_WIN64) || defined(_WIN32))
#  include "aether/port/file_systems/file_system_std.h"
#  include "aether/adapters/ethernet.h"
#elif (defined(ESP_PLATFORM))
#  include "aether/port/file_systems/file_system_ram.h"
#  include "aether/adapters/esp32_wifi.h"
#endif

#include "aether/tele/tele.h"
#include "aether/tele/ios_time.h"

using std::vector;

static constexpr char WIFI_SSID[] = "Test123";
static constexpr char WIFI_PASS[] = "Test123";

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
  adapter->Connect();
#  else
  #error "Unsupported configuration specified"
#  endif

  aether->adapter_factories.emplace_back(std::move(adapter));

#  if AE_SUPPORT_REGISTRATION
  // localhost
  aether->registration_cloud->AddServerSettings(IpAddressPortProtocol{
      {IpAddress{IpAddress::Version::kIpV4, {127, 0, 0, 1}}, 9010},
      Protocol::kTcp});
  // cloud address
  aether->registration_cloud->AddServerSettings(IpAddressPortProtocol{
      {IpAddress{IpAddress::Version::kIpV4, {35, 224, 1, 127}}, 9010},
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

void AetherCloudExample();

void AetherCloudExample() {
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
#elif ((defined(ESP_PLATFORM)))
      ae::FileSystemRamFacility{};
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

  ae::Client::ptr client_sender;
  ae::Client::ptr client_receiver;

  // Create ad client and perform registration
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

    auto reg1 =
        registration1->SubscribeOnResult([&client_sender](auto const& action) {
          client_sender = action.client();
        });
    auto reg1_failed = registration1->SubscribeOnError([&](auto const&) {
      AE_TELED_ERROR("Registration rejected");
      reg_failed = true;
    });

    auto reg2 = registration2->SubscribeOnResult(
        [&client_receiver](auto const& action) {
          client_receiver = action.client();
        });
    auto reg2_failed = registration2->SubscribeOnError([&](auto const&) {
      AE_TELED_ERROR("Registration rejected");
      reg_failed = true;
    });

    while ((!client_sender || !client_receiver) && !reg_failed) {
      auto next_time = aether->domain_->Update(ae::Now());
      AE_TELED_DEBUG(
          "Waiting for registration from sender {}, receiver {}, reg_failed "
          "{}, "
          "{} until {}",
          static_cast<bool>(client_sender), static_cast<bool>(client_receiver),
          reg_failed, ae::FormatTimePoint("%H:%M:%S", ae::Now()),
          ae::FormatTimePoint("%H:%M:%S", next_time));
      aether->action_processor->get_trigger().WaitUntil(next_time);
    }
    if (reg_failed) {
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

  int receive_count = 0;
  int confirm_count = 0;
  std::vector<std::string> messages = {
      "Hello, it's me",
      "I was wondering if, after all these years, you'd like to meet",
      "To go over everything",
      "They say that time's supposed to heal ya",
      "But I ain't done much healin'",
      "Hello, can you hear me?",
      "I'm in California dreaming about who we used to be",
      "When we were younger and free",
      "I've forgotten how it felt before the world fell at our feet"};

  auto receiver_connection = client_receiver->client_connection();

  ae::SafeStreamConfig config;
  config.buffer_capacity = std::numeric_limits<std::uint16_t>::max();
  config.max_repeat_count = 4;
  config.window_size = (config.buffer_capacity / 2) - 1;
  config.wait_confirm_timeout = std::chrono::milliseconds{200};
  config.send_confirm_timeout = {};
  config.send_repeat_timeout = std::chrono::milliseconds{200};
  config.max_data_size = config.window_size - 1;

  ae::Ptr<ae::P2pSafeStream> receiver_stream;
  ae::Subscription receiver_subscription;
  auto _s0 = receiver_connection->new_stream_event().Subscribe(
      [&](auto uid, auto stream_id, auto const& message_stream) {
        AE_TELED_DEBUG("Received stream from {}", uid);
        receiver_stream = ae::MakePtr<ae::P2pSafeStream>(
            *aether->action_processor, config,
            ae::MakePtr<ae::P2pStream>(*aether->action_processor,
                                       client_receiver, uid, stream_id,
                                       message_stream));
        receiver_subscription =
            receiver_stream->in().out_data_event().Subscribe(
                [&](auto const& data) {
                  auto str_msg = std::string(
                      reinterpret_cast<const char*>(data.data()), data.size());
                  AE_TELED_DEBUG("Received a message [{}]", str_msg);
                  auto msgit = std::find(std::begin(messages),
                                         std::end(messages), str_msg);
                  if (msgit != std::end(messages)) {
                    receive_count++;
                  }
                  auto confirm_msg = std::string{"confirmed "} + str_msg;
                  receiver_stream->in().Write(
                      {confirm_msg.data(),
                       confirm_msg.data() + confirm_msg.size()},
                      ae::Now());
                });
      });

  auto sender_stream = ae::MakePtr<ae::P2pSafeStream>(
      *aether->action_processor, config,
      ae::MakePtr<ae::P2pStream>(*aether->action_processor, client_sender,
                                 client_receiver->uid(), ae::StreamId{0}));

  AE_TELED_DEBUG("SEND MESSAGES FROM SENDER TO RECEIVER");
  for (auto const& message : messages) {
    sender_stream->in().Write({std::begin(message), std::end(message)},
                              ae::Now());
  }

  auto _s1 =
      sender_stream->in().out_data_event().Subscribe([&](auto const& data) {
        auto str_response = std::string(
            reinterpret_cast<const char*>(data.data()), data.size());
        AE_TELED_DEBUG("Received a response [{}], confirm_count {}",
                       str_response, confirm_count);
        confirm_count++;
      });

  while ((receive_count < messages.size()) ||
         (confirm_count < messages.size())) {
    AE_TELED_DEBUG("receive_count {} confirm_count {}", receive_count,
                   confirm_count);
    auto current_time = ae::Now();
    auto next_time = aether->domain_->Update(current_time);
    aether->action_processor->get_trigger().WaitUntil(
        std::min(next_time, current_time + std::chrono::seconds{5}));
  }

  aether->domain_->Update(ae::ClockType::now());

  // save objects state
  domain.SaveRoot(aether);
}
