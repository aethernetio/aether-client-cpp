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

#include "aether/ptr/ptr.h"
#include "aether/obj/domain.h"
#include "aether/literal_array.h"
#include "aether/actions/action.h"
#include "aether/events/barrier_event.h"
#include "aether/events/event_subscription.h"
#include "aether/events/multi_subscription.h"

#include "aether/aether.h"
#include "aether/client.h"
#include "aether/global_ids.h"
#include "aether/aether_app.h"
#include "aether/registration_cloud.h"

#include "send_message_delays/receiver.h"
#include "send_message_delays/sender.h"
#include "send_message_delays/statistics_write.h"
#include "send_message_delays/send_message_delays_manager.h"

#include "aether/tele/tele.h"

namespace ae::bench {
class TestSendMessageDelaysAction : public Action<TestSendMessageDelaysAction> {
  enum class State : std::uint8_t {
    kRegisterClients,
    kMakeTest,
    kResult,
    kError,
  };

 public:
  TestSendMessageDelaysAction(Ptr<AetherApp> aether_app,
                              std::ostream& write_results_stream)
      : Action{*aether_app},
        aether_{aether_app->aether()},
        write_results_stream_{write_results_stream},
        state_{State::kRegisterClients},
        state_changed_{state_.changed_event().Subscribe(
            [this](auto) { Action::Trigger(); })} {}

  TimePoint Update(TimePoint current_time) override {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kRegisterClients:
          RegisterClients();
          return current_time;
        case State::kMakeTest:
          MakeTest();
          break;
        case State::kResult:
          Action::Result(*this);
          return current_time;
        case State::kError:
          Action::Error(*this);
          return current_time;
      }
    }
    return current_time;
  }

 private:
  void RegisterClients() {
#if AE_SUPPORT_REGISTRATION
    aether_->clients().clear();
    auto reg_sender = aether_->RegisterClient(
        Uid{MakeLiteralArray("3ac931653d37497087a6fa4ee27744e4")});
    auto reg_receiver = aether_->RegisterClient(
        Uid{MakeLiteralArray("3ac931653d37497087a6fa4ee27744e4")});

    registration_subscriptions_.Push(
        reg_sender->SubscribeOnResult([this](auto const& action) {
          AE_TELED_INFO("Sender registered");
          client_sender_ = action.client();
          clients_registered_event_.Emit<0>();
        }),
        reg_sender->SubscribeOnError(
            [this](auto const&) { state_ = State::kError; }),
        reg_receiver->SubscribeOnResult([this](auto const& action) {
          AE_TELED_INFO("Receiver registered");
          client_receiver_ = action.client();
          clients_registered_event_.Emit<1>();
        }),
        reg_receiver->SubscribeOnError(
            [this](auto const&) { state_ = State::kError; }),
        clients_registered_event_.Subscribe([this]() {
          AE_TELED_INFO("All clients registered");
          state_ = State::kMakeTest;
        }));
#else
    assert(aether_->clients().size() >= 2);
    client_sender_ = aether_->clients()[0];
    client_receiver_ = aether_->clients()[1];
    state_ = State::kMakeTest;
#endif
  }

  void MakeTest() {
    AE_TELED_INFO("Make a test");
    SafeStreamConfig safe_stream_config{
        std::numeric_limits<std::uint16_t>::max(),
        (std::numeric_limits<std::uint16_t>::max() / 2) - 1,
        (std::numeric_limits<std::uint16_t>::max() / 2) - 1,
        10,
        std::chrono::milliseconds{400},
        std::chrono::milliseconds{0},
        std::chrono::milliseconds{200},
    };

    auto sender = make_unique<Sender>(ActionContext{*aether_->action_processor},
                                      client_sender_, client_receiver_->uid(),
                                      safe_stream_config);
    auto receiver =
        make_unique<Receiver>(ActionContext{*aether_->action_processor},
                              client_receiver_, safe_stream_config);

    send_message_delays_manager_ = make_unique<SendMessageDelaysManager>(
        ActionContext{*aether_->action_processor}, std::move(sender),
        std::move(receiver));

    auto test_action = send_message_delays_manager_->Test({
        /* WarUp message count */ 100,
        /* test message count */ 300,
        /* min send interval */ std::chrono::milliseconds{50},
    });

    test_result_subscriptions_.Push(
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
          Format(write_results_stream_, "{}",
                 StatisticsWriteCsv{std::move(statistics_write_list)});

          state_ = State::kResult;
        }),
        test_action->SubscribeOnError([&](auto const&) {
          AE_TELED_ERROR("Test failed");
          state_ = State::kError;
        }));
  }

  Aether::ptr aether_;
  std::ostream& write_results_stream_;
  Client::ptr client_sender_;
  Client::ptr client_receiver_;
  std::unique_ptr<SendMessageDelaysManager> send_message_delays_manager_;

  BarrierEvent<void, 2> clients_registered_event_;
  MultiSubscription registration_subscriptions_;
  MultiSubscription test_result_subscriptions_;
  StateMachine<State> state_;
  Subscription state_changed_;
};

int test_send_message_delays(std::ostream& result_stream) {
  auto aether_app = AetherApp::Construct(AetherAppConstructor{});

  auto test_action = TestSendMessageDelaysAction{aether_app, result_stream};
  auto success =
      test_action.SubscribeOnResult([&](auto const&) { aether_app->Exit(0); });
  auto failed =
      test_action.SubscribeOnError([&](auto const&) { aether_app->Exit(1); });

  while (!aether_app->IsExited()) {
    auto time = Now();
    auto next_time = aether_app->Update(time);
    aether_app->WaitUntil(std::min(next_time, time + std::chrono::seconds(5)));
  }

  return aether_app->ExitCode();
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
