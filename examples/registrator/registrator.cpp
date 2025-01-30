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

#include <string>
#include <tuple>
#include <vector>

#include "aether/aether.h"
#include "aether/common.h"
#include "aether/literal_array.h"
#include "third_party/ini.h/ini.h"

#include "aether/global_ids.h"
#include "aether/aether_app.h"
#include "aether/ae_actions/registration/registration.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/client_messages/p2p_safe_message_stream.h"

#include "aether/port/file_systems/file_system_header.h"
#include "aether/adapters/register_wifi.h"

#include "aether/port/tele_init.h"
#include "aether/tele/tele.h"

constexpr std::uint8_t clients_max = 4;
constexpr std::uint8_t servers_max = 4;

namespace ae {
enum class ServerAddressType : std::uint8_t { kIpAddress, kUrlAddress };

struct ServerConfig {
  ae::ServerAddressType server_address_type;
  ae::IpAddress::Version server_ip_address_version;
  std::string server_address;
  std::uint16_t server_port;
  ae::Protocol server_protocol;
};
}  // namespace ae

namespace ae::registrator {
constexpr ae::SafeStreamConfig kSafeStreamConfig{
    std::numeric_limits<std::uint16_t>::max(),                // buffer_capacity
    (std::numeric_limits<std::uint16_t>::max() / 2) - 1,      // window_size
    (std::numeric_limits<std::uint16_t>::max() / 2) - 1 - 1,  // max_data_size
    4,                               // max_repeat_count
    std::chrono::milliseconds{200},  // wait_confirm_timeout
    {},                              // send_confirm_timeout
    std::chrono::milliseconds{200},  // send_repeat_timeout
};

/**
 * \brief This is the main action to perform all busyness logic here.
 */
class RegistratorAction : public Action<RegistratorAction> {
  enum class State : std::uint8_t {
    kRegistration,
    kConfigureReceiver,
    kConfigureSender,
    kSendMessages,
    kWaitDone,
    kResult,
    kError,
  };

 public:
  explicit RegistratorAction(Ptr<AetherApp> const& aether_app)
      : Action{*aether_app},
        aether_{aether_app->aether()},
        state_{State::kRegistration},
        messages_{},
        state_changed_{state_.changed_event().Subscribe(
            [this](auto) { Action::Trigger(); })} {
    AE_TELED_INFO("Cloud test");
  }

  TimePoint Update(TimePoint current_time) override {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kRegistration:
          RegisterClients();
          break;
        case State::kConfigureReceiver:
          ConfigureReceiver();
          break;
        case State::kConfigureSender:
          ConfigureSender();
          break;
        case State::kSendMessages:
          SendMessages(current_time);
          break;
        case State::kWaitDone:
          break;
        case State::kResult:
          Action::Result(*this);
          return current_time;
        case State::kError:
          Action::Error(*this);
          return current_time;
      }
    }
    // wait till all sent messages received and confirmed
    if (state_.get() == State::kWaitDone) {
      AE_TELED_DEBUG("Wait done receive_count {}, confirm_count {}",
                     receive_count_, confirm_count_);
      if ((receive_count_ == messages_.size()) &&
          (confirm_count_ == messages_.size())) {
        state_ = State::kResult;
      }
      // if no any events happens wake up after 1 second
      return current_time + std::chrono::seconds{1};
    }
    return current_time;
  }

 private:
  /**
   * \brief Perform a client registration.
   * We need a two clients for this test.
   */
  void RegisterClients() {
#if AE_SUPPORT_REGISTRATION
    {
      AE_TELED_INFO("Client registration");
      // register receiver and sender
      clients_registered_ = aether_->clients().size();

      for (auto i = clients_registered_; i < 2; i++) {
        auto reg_action = aether_->RegisterClient(
            Uid{MakeLiteralArray("3ac931653d37497087a6fa4ee27744e4")});
        registration_subscriptions_.Push(
            reg_action->SubscribeOnResult([&](auto const&) {
              ++clients_registered_;
              if (clients_registered_ == 2) {
                state_ = State::kConfigureReceiver;
              }
            }),
            reg_action->SubscribeOnError([&](auto const&) {
              AE_TELED_ERROR("Registration error");
              state_ = State::kError;
            }));
      }
      // all required clients already registered
      if (clients_registered_ == 2) {
        state_ = State::kConfigureReceiver;
        return;
      }
    }
#else
    // skip registration
    state_ = State::kConfigureReceiver;
#endif
  }

  /**
   * \brief Make required preparation for receiving messages.
   * Subscribe to opening new stream event.
   * Subscribe to receiving message event.
   * Send confirmation to received message.
   */
  void ConfigureReceiver() {
    AE_TELED_INFO("Receiver configuration");
    receive_count_ = 0;
    assert(!aether_->clients().empty());
    receiver_ = aether_->clients()[0];
    auto receiver_connection = receiver_->client_connection();
    receiver_new_stream_subscription_ =
        receiver_connection->new_stream_event().Subscribe(
            [&](auto uid, auto stream_id, auto raw_stream) {
              receiver_stream_ = MakePtr<P2pSafeStream>(
                  *aether_->action_processor, kSafeStreamConfig,
                  MakePtr<P2pStream>(*aether_->action_processor, receiver_, uid,
                                     stream_id, std::move(raw_stream)));
              receiver_message_subscription_ =
                  receiver_stream_->in().out_data_event().Subscribe(
                      [&](auto const& data) {
                        auto str_msg = std::string(
                            reinterpret_cast<const char*>(data.data()),
                            data.size());
                        AE_TELED_DEBUG("Received a message [{}]", str_msg);
                        receive_count_++;
                        auto confirm_msg = std::string{"confirmed "} + str_msg;
                        auto response_action = receiver_stream_->in().Write(
                            {confirm_msg.data(),
                             confirm_msg.data() + confirm_msg.size()},
                            ae::Now());
                        response_subscriptions_.Push(
                            response_action->SubscribeOnError([&](auto const&) {
                              AE_TELED_ERROR("Send response failed");
                              state_ = State::kError;
                            }));
                      });
            });
    state_ = State::kConfigureSender;
  }

  /**
   * \brief Make required preparation to send messages.
   * Create a sender to receiver stream.
   * Subscribe to receiving message event for confirmations \see
   * ConfigureReceiver.
   */
  void ConfigureSender() {
    AE_TELED_INFO("Sender configuration");
    confirm_count_ = 0;
    assert(aether_->clients().size() > 1);
    sender_ = aether_->clients()[1];
    sender_stream_ = MakePtr<P2pSafeStream>(
        *aether_->action_processor, kSafeStreamConfig,
        MakePtr<P2pStream>(*aether_->action_processor, sender_,
                           receiver_->uid(), StreamId{0}));
    sender_message_subscription_ =
        sender_stream_->in().out_data_event().Subscribe([&](auto const& data) {
          auto str_response = std::string(
              reinterpret_cast<const char*>(data.data()), data.size());
          AE_TELED_DEBUG("Received a response [{}], confirm_count {}",
                         str_response, confirm_count_);
          confirm_count_++;
        });
    state_ = State::kSendMessages;
  }

  /**
   * \brief Send all messages at once.
   */
  void SendMessages(TimePoint current_time) {
    AE_TELED_INFO("Send messages");
    for (auto const& msg : messages_) {
      auto send_action = sender_stream_->in().Write(
          DataBuffer{std::begin(msg), std::end(msg)}, current_time);
      send_subscriptions_.Push(send_action->SubscribeOnError([&](auto const&) {
        AE_TELED_ERROR("Send message failed");
        state_ = State::kError;
      }));
    }
    state_ = State::kWaitDone;
  }

  Aether::ptr aether_;
  std::vector<std::string> messages_;

  Client::ptr receiver_;
  Ptr<ByteStream> receiver_stream_;
  Client::ptr sender_;
  Ptr<ByteStream> sender_stream_;
  std::size_t clients_registered_;
  std::size_t receive_count_;
  std::size_t confirm_count_;

  MultiSubscription registration_subscriptions_;
  Subscription receiver_new_stream_subscription_;
  Subscription receiver_message_subscription_;
  MultiSubscription response_subscriptions_;
  Subscription sender_message_subscription_;
  MultiSubscription send_subscriptions_;
  StateMachine<State> state_;
  Subscription state_changed_;
};

}  // namespace ae::registrator

//**********************************************************************************************************************************
int AetherRegistrator(const std::string& ini_file);

int AetherRegistrator(const std::string& ini_file) {
  ae::TeleInit::Init();
  {
    AE_TELE_ENV();
    AE_TELE_INFO("Started");
    ae::Registry::Log();
  }

  // Reading settings from the ini file.
  ini::File file = ini::open(ini_file);

  std::string wifi_ssid = file["Aether"]["wifiSsid"];
  std::string wifi_pass = file["Aether"]["wifiPass"];

  AE_TELED_DEBUG("WiFi ssid={}", wifi_ssid);
  AE_TELED_DEBUG("WiFi pass={}", wifi_pass);

  std::string sodium_key = file["Aether"]["sodiumKey"];
  std::string hydrogen_key = file["Aether"]["hydrogenKey"];

  AE_TELED_DEBUG("Sodium key={}", sodium_key);
  AE_TELED_DEBUG("Hydrogen key={}", hydrogen_key);

  // Clients configuration
  std::int8_t parents_num = file["Aether"].get<int>("parentsNum");

  std::string uid;
  std::uint8_t clients_num, clients_total{0};
  std::tuple<std::string, std::uint8_t> parent;
  std::vector<std::tuple<std::string, std::int8_t>> parents;

  for (std::uint8_t i{0}; i < parents_num; i++) {
    uid = file["ParentID" + std::to_string(i + 1)]["uid"];
    clients_num =
        file["ParentID" + std::to_string(i + 1)].get<int>("clientsNum");
    parent = make_tuple(uid, clients_num);
    parents.push_back(parent);
    clients_total += clients_num;
  }

  // Clients max assertion
  if (clients_total > clients_max) {
    std::cerr << "Total clients must be < " << clients_max << " clients\n";
    return -1;
  }

  // Servers configuration
  std::int8_t servers_num = file["Aether"].get<int>("serversNum");

  std::uint8_t servers_total{0};
  std::vector<ae::ServerConfig> servers;
  ae::ServerConfig server_config{};

  for (std::uint8_t i{0}; i < servers_num; i++) {
    std::string str_ip_address_type =
        file["ServerID" + std::to_string(i + 1)]["ipAddressType"];
    std::string str_ip_address_version =
        file["ServerID" + std::to_string(i + 1)]["ipAddressVersion"];
    std::string str_ip_address =
        file["ServerID" + std::to_string(i + 1)]["ipAddress"];
    std::uint16_t int_ip_port =
        file["ServerID" + std::to_string(i + 1)].get<int>("ipPort");
    std::string str_ip_protocol =
        file["ServerID" + std::to_string(i + 1)]["ipProtocol"];

    if (str_ip_address_type == "kIpAddress") {
      server_config.server_address_type = ae::ServerAddressType::kIpAddress;
      if (str_ip_address_version == "kIpV4") {
        server_config.server_ip_address_version = ae::IpAddress::Version::kIpV4;
      }
      server_config.server_address = str_ip_address;
      server_config.server_port = int_ip_port;
      if (str_ip_protocol == "kTcp") {
        server_config.server_protocol = ae::Protocol::kTcp;
      }
    } else if (str_ip_address_type == "kUrlAddress") {
      server_config.server_address_type = ae::ServerAddressType::kUrlAddress;
      if (str_ip_address_version == "kIpV4") {
        server_config.server_ip_address_version = ae::IpAddress::Version::kIpV4;
      }
      server_config.server_address = str_ip_address;
      server_config.server_port = int_ip_port;
      if (str_ip_protocol == "kTcp") {
        server_config.server_protocol = ae::Protocol::kTcp;
      }
    }

    servers.push_back(server_config);
    servers_total++;
  }

  // Servers max assertion
  if (servers_total > servers_max) {
    std::cerr << "Total servers must be < " << servers_max << " servers\n";
    return -2;
  }

  /**
   * Construct a main aether application class.
   * It's include a Domain and Aether instances accessible by getter methods.
   * It has Update, WaitUntil, Exit, IsExit, ExitCode methods to integrate it in
   * your update loop.
   * Also it has action context protocol implementation \see Action.
   * To configure its creation \see AetherAppConstructor.
   */
  auto aether_app = ae::AetherApp::Construct(
      ae::AetherAppConstructor{}
#if defined AE_DISTILLATION
          .Adapter([](ae::Ptr<ae::Domain> const& domain,
                      ae::Aether::ptr const& aether) -> ae::Adapter::ptr {
#  if defined ESP32_WIFI_ADAPTER_ENABLED
            auto adapter = domain.CreateObj<ae::Esp32WifiAdapter>(
                ae::GlobalId::kEsp32WiFiAdapter, aether, aether->poller,
                std::string(WIFI_SSID), std::string(WIFI_PASS));
#  else
            auto adapter = domain->CreateObj<ae::EthernetAdapter>(
                ae::GlobalId::kEthernetAdapter, aether, aether->poller);
#  endif
            return adapter;
          })
#  if AE_SUPPORT_REGISTRATION
          .RegCloud([servers](ae::Ptr<ae::Domain> const& domain,
                              ae::Aether::ptr const& /* aether */) {
            auto registration_cloud = domain->CreateObj<ae::RegistrationCloud>(
                ae::kRegistrationCloud);
            for (auto s : servers) {
              AE_TELED_DEBUG("Server address type={}", s.server_address_type);
              AE_TELED_DEBUG("Server ip address version={}",
                             s.server_ip_address_version);
              AE_TELED_DEBUG("Server address={}", s.server_address);
              AE_TELED_DEBUG("Server port={}", s.server_port);
              AE_TELED_DEBUG("Server protocol={}", s.server_protocol);

              if (s.server_address_type == ae::ServerAddressType::kIpAddress) {
                registration_cloud->AddServerSettings(ae::IpAddressPortProtocol{
                    {ae::IpAddress{s.server_ip_address_version, {127, 0, 0, 1}},
                     s.server_port},
                    s.server_protocol});
              } else if (s.server_address_type ==
                         ae::ServerAddressType::kUrlAddress) {
                registration_cloud->AddServerSettings(ae::NameAddress{
                    s.server_address, s.server_port, s.server_protocol});
              }
            }

            return registration_cloud;
          })
#  endif  // AE_SUPPORT_REGISTRATION
#endif
  );

  auto registrator_action = ae::registrator::RegistratorAction{aether_app};

  auto success = registrator_action.SubscribeOnResult(
      [&](auto const&) { aether_app->Exit(0); });
  auto failed = registrator_action.SubscribeOnError(
      [&](auto const&) { aether_app->Exit(1); });

  while (!aether_app->IsExited()) {
    auto current_time = ae::Now();
    auto next_time = aether_app->Update(current_time);
    aether_app->WaitUntil(
        std::min(next_time, current_time + std::chrono::seconds{5}));
  }

  return aether_app->ExitCode();

  /*  ae::TeleInit::Init();

    {
      AE_TELE_ENV();
      AE_TELE_INFO("Started");
      ae::Registry::Log();
    }

    // Reading settings from the ini file.
    ini::File file = ini::open(ini_file);

    std::string wifi_ssid = file["Aether"]["wifiSsid"];
    std::string wifi_pass = file["Aether"]["wifiPass"];

    AE_TELED_DEBUG("WiFi ssid={}", wifi_ssid);
    AE_TELED_DEBUG("WiFi pass={}", wifi_pass);

    std::string sodium_key = file["Aether"]["sodiumKey"];
    std::string hydrogen_key = file["Aether"]["hydrogenKey"];

    AE_TELED_DEBUG("Sodium key={}", sodium_key);
    AE_TELED_DEBUG("Hydrogen key={}", hydrogen_key);

    std::int8_t parents_num = file["Aether"].get<int>("parentsNum");

    std::string uid;
    std::uint8_t clients_num, clients_total{0};
    std::tuple<std::string, std::uint8_t> parent;
    std::vector<std::tuple<std::string, std::int8_t>> parents;

    for (std::uint8_t i{0}; i < parents_num; i++) {
      uid = file["ParentID" + std::to_string(i + 1)]["uid"];
      clients_num =
          file["ParentID" + std::to_string(i + 1)].get<int>("clientsNum");
      parent = make_tuple(uid, clients_num);
      parents.push_back(parent);
      clients_total += clients_num;
    }

    // Clients max assertion
    if (clients_total > clients_max) {
      std::cerr << "Total clients must be < " << clients_max << " clients\n";
      return -1;
    }

    auto fs = ae::FileSystemHeaderFacility{};

  #ifdef AE_DISTILLATION
    // create objects in instrument mode
    {
      ae::Domain domain{ae::ClockType::now(), fs};
      fs.remove_all();
      ae::Aether::ptr aether =
          ae::CreateAetherInstrument(domain, wifi_ssid, wifi_pass);
  #  if AE_SIGNATURE == AE_ED25519
      auto sspk_str = sodium_key;
      auto sspk_arr = ae::MakeArray(sspk_str);
      auto sspk = ae::SodiumSignPublicKey{};
      if (sspk_arr.size() != sspk.key.size()) {
        std::cerr << "SodiumSignPublicKey size must be " << sspk.key.size()
                  << " bytes\n";
        return -2;
      }

      std::copy(std::begin(sspk_arr), std::end(sspk_arr), std::begin(sspk.key));
      aether->crypto->signs_pk_[ae::SignatureMethod::kEd25519] = sspk;
  #  elif AE_SIGNATURE == AE_HYDRO_SIGNATURE
      auto hspk_str = hydrogen_key;
      auto hspk_arr = ae::MakeArray(hspk_str);
      auto hspk = ae::HydrogenSignPublicKey{};
      if (hspk_arr.size() != hspk.key.size()) {
        std::cerr << "HydrogenSignPublicKey size must be " << hspk.key.size()
                  << " bytes\n";
        return -2;
      }

      std::copy(std::begin(hspk_arr), std::end(hspk_arr), std::begin(hspk.key));
      aether->crypto->signs_pk_[ae::SignatureMethod::kHydroSignature] = hspk;
  #  endif  // AE_SIGNATURE == AE_ED25519
      domain.SaveRoot(aether);
    }
  #endif  // AE_DISTILLATION

    ae::Domain domain{ae::ClockType::now(), fs};
    ae::Aether::ptr aether = ae::LoadAether(domain);
    ae::TeleInit::Init(aether);

    ae::Adapter::ptr
  adapter{domain.LoadCopy(aether->adapter_factories.front())};

    ae::Client::ptr client;

    // Creating the actual adapter.
    auto& cloud = aether->registration_cloud;
    domain.LoadRoot(cloud);
    cloud->set_adapter(adapter);

    for (auto p : parents) {
      std::string uid_str = std::get<0>(p);
      std::uint8_t clients_num = std::get<1>(p);
      for (std::uint8_t i{0}; i < clients_num; i++) {
  #if AE_SUPPORT_REGISTRATION
        auto uid_arr = ae::MakeArray(uid_str);
        if (uid_arr.size() != ae::Uid::kSize) {
          std::cerr << "Uid size must be " << ae::Uid::kSize << " bytes\n";
          return -3;
        }
        auto uid = ae::Uid{};
        std::copy(std::begin(uid_arr), std::end(uid_arr),
  std::begin(uid.value)); auto registration = aether->RegisterClient(uid);

        bool register_done = false;
        bool register_failed = false;

        auto reg = registration->SubscribeOnResult([&](auto const& reg) {
          register_done = true;
          client = reg.client();
        });
        auto reg_failed = registration->SubscribeOnError(
            [&](auto const&) { register_failed = true; });

        while (!register_done && !register_failed) {
          auto time = ae::TimePoint::clock::now();
          auto next_time = domain.Update(time);
          aether->action_processor->get_trigger().WaitUntil(next_time);

          if (register_failed) {
            std::cerr << "Registration failed\n";
            return -4;
          }
        }
  #endif
      }
    }

    // save objects state
    domain.SaveRoot(aether);

    return 0; */
}