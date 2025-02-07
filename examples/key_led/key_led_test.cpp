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

#include "aether/common.h"
#include "aether/state_machine.h"
#include "aether/literal_array.h"
#include "aether/actions/action.h"
#include "aether/stream_api/istream.h"
#include "aether/transport/data_buffer.h"
#include "aether/events/event_subscription.h"
#include "aether/events/multi_subscription.h"

#include "aether/global_ids.h"
#include "aether/aether_app.h"
#include "aether/ae_actions/registration/registration.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether/client_messages/p2p_safe_message_stream.h"

#include "aether/port/tele_init.h"

#include "aether/adapters/ethernet.h"
#include "aether/adapters/esp32_wifi.h"

#include "aether/tele/tele.h"
#include "aether/tele/ios_time.h"

#include "led_button_nix.h"
#include "led_button_win.h"
#include "led_button_mac.h"
#include "led_button_esp.h"

using std::vector;

static constexpr char WIFI_SSID[] = "Test123";
static constexpr char WIFI_PASS[] = "Test123";
static constexpr bool use_aether = true;

namespace ae::key_led_test {
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
class KeyLedTestAction : public Action<KeyLedTestAction> {
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
  explicit KeyLedTestAction(Ptr<AetherApp> const& aether_app)
      : Action{*aether_app},
        aether_{aether_app->aether()},
        state_{State::kRegistration},
        messages_{"LED on", "LED off"},
        state_changed_{state_.changed_event().Subscribe(
            [this](auto) { Action::Trigger(); })} {
    AE_TELED_INFO("Key to Led test");

    button_action_ =
#if defined BUTTON_NIX
        ae::LedButtonNix{ae::ActionContext {
          *aether_->action_processor
        }};
#elif defined BUTTON_WIN
        ae::LedButtonWin{ae::ActionContext {
          *aether_->action_processor
        }};
#elif defined BUTTON_MAC
        ae::LedButtonMac{ae::ActionContext {
          *aether_->action_processor
        }};
#elif defined BUTTON_ESP
        ae::LedButtonEsp{ae::ActionContext {
          *aether_->action_processor
        }};
#endif
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

    state_ = State::kSendMessages;
  }

  /**
   * \brief Send all messages at once.
   */
  void SendMessages(TimePoint current_time) {
    AE_TELED_INFO("Send messages");

    state_ = State::kWaitDone;
  }

  Aether::ptr aether_;

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
  std::vector<std::string> messages_;
  Subscription state_changed_;

#if defined BUTTON_NIX
  ae::LedButtonNix button_action_;
#elif defined BUTTON_WIN
  ae::LedButtonWin button_action_;
#elif defined BUTTON_MAC
  ae::LedButtonMac button_action_;
#elif defined BUTTON_ESP
  ae::LedButtonEsp button_action_;
#endif
};

}  // namespace ae::key_led_test

/*static constexpr char WIFI_SSID[] = "Test123";
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

}  // namespace ae*/

void AetherButtonExample();
//*******************************************************************************************************************************

void AetherButtonExample() {
  /*  ae::TeleInit::Init();
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

    ae::Adapter::ptr
  adapter{domain.LoadCopy(aether->adapter_factories.front())};

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
                        reinterpret_cast<const char*>(data.data()),
  data.size()); AE_TELED_DEBUG("Received a message {}", str_msg); if
  ((str_msg.compare(messages[0])) == 0) { button_action.SetLed(1);
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
              sender_to_receiver_stream->in().Write(
                  {std::begin(messages[0]), std::end(messages[0])},
                  ae::TimePoint::clock::now());
            } else {
              button_action.SetLed(1);
            }
          } else {
            AE_TELED_INFO("Low level Press");
            if (use_aether) {
              sender_to_receiver_stream->in().Write(
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
    domain.SaveRoot(aether);*/
}
