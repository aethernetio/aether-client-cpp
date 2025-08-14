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

#include "aether/all.h"

#include "send_message_delays/receiver.h"
#include "send_message_delays/sender.h"
#include "send_message_delays/statistics_write.h"
#include "send_message_delays/send_message_delays_manager.h"

namespace ae::bench {
class TestSendMessageDelaysAction : public Action<TestSendMessageDelaysAction> {
  enum class State : std::uint8_t {
    kRegisterClients,
    kMakeTest,
    kResult,
    kError,
  };

 public:
  TestSendMessageDelaysAction(RcPtr<AetherApp> aether_app,
                              std::ostream& write_results_stream)
      : Action{*aether_app},
        aether_{aether_app->aether()},
        write_results_stream_{write_results_stream},
        state_{State::kRegisterClients},
        state_changed_{state_.changed_event().Subscribe(
            [this](auto) { Action::Trigger(); })} {}

  ActionResult Update() {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kRegisterClients:
          GetClients();
          break;
        case State::kMakeTest:
          MakeTest();
          break;
        case State::kResult:
          return ActionResult::Result();
        case State::kError:
          return ActionResult::Error();
      }
    }
    return {};
  }

 private:
  void GetClients() {
    auto get_sender = aether_->SelectClient(
        Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4"), 1);
    auto get_receiver = aether_->SelectClient(
        Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4"), 2);

    client_selected_event_.Connect([](auto& action) { return action.client(); },
                                   get_sender->ResultEvent(),
                                   get_receiver->ResultEvent());

    registration_subscriptions_.Push(
        get_sender->ErrorEvent().Subscribe(
            [this](auto const&) { state_ = State::kError; }),
        get_receiver->ErrorEvent().Subscribe(
            [this](auto const&) { state_ = State::kError; }),
        client_selected_event_.Subscribe([this](auto const& event) {
          AE_TELED_INFO("All clients acquired");
          client_sender_ = event[0];
          client_receiver_ = event[1];
          state_ = State::kMakeTest;
        }));
  }

  void MakeTest() {
    AE_TELED_INFO("Make a test");
    SafeStreamConfig safe_stream_config{
        std::numeric_limits<std::uint16_t>::max(),
        (std::numeric_limits<std::uint16_t>::max() / 2) - 1,
        (std::numeric_limits<std::uint16_t>::max() / 2) - 1,
        10,
        std::chrono::milliseconds{1500},
        std::chrono::milliseconds{0},
        std::chrono::milliseconds{200},
    };

    auto sender =
        make_unique<Sender>(ActionContext{*aether_}, client_sender_,
                            client_receiver_->uid(), safe_stream_config);
    auto receiver = make_unique<Receiver>(ActionContext{*aether_},
                                          client_receiver_, safe_stream_config);

    send_message_delays_manager_ = make_unique<SendMessageDelaysManager>(
        ActionContext{*aether_}, std::move(sender), std::move(receiver));

    static constexpr auto test_message_count = 100;
    auto test_action = send_message_delays_manager_->Test({
        /* WarUp message count */ 10,
        /* test message count */ test_message_count,
        /* min send interval */ std::chrono::milliseconds{1000},
    });

    test_result_subscriptions_.Push(
        test_action->ResultEvent().Subscribe([&](auto const& action) {
          auto res_name_table = std::array{
              std::string_view{"p2p 2 Bytes"},
              std::string_view{"p2p 10 Bytes"},
              std::string_view{"p2p 100 Bytes"},
              std::string_view{"p2p 1000 Bytes"},
              std::string_view{"p2pss 2 Bytes"},
              std::string_view{"p2pss 10 Bytes"},
              std::string_view{"p2pss 100 Bytes"},
              std::string_view{"p2pss 1000 Bytes"},
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
                 StatisticsWriteCsv{std::move(statistics_write_list),
                                    test_message_count});

          state_ = State::kResult;
        }),
        test_action->ErrorEvent().Subscribe([&](auto const&) {
          AE_TELED_ERROR("Test failed");
          state_ = State::kError;
        }));
  }

  Aether::ptr aether_;
  std::ostream& write_results_stream_;
  Client::ptr client_sender_;
  Client::ptr client_receiver_;
  std::unique_ptr<SendMessageDelaysManager> send_message_delays_manager_;

  CumulativeEvent<Client::ptr, 2> client_selected_event_;
  MultiSubscription registration_subscriptions_;
  MultiSubscription test_result_subscriptions_;
  StateMachine<State> state_;
  Subscription state_changed_;
};

int test_send_message_delays(std::ostream& result_stream) {
  auto aether_app = AetherApp::Construct(AetherAppContext{});

  auto test_action = TestSendMessageDelaysAction{aether_app, result_stream};
  auto success = test_action.ResultEvent().Subscribe(
      [&](auto const&) { aether_app->Exit(0); });
  auto failed = test_action.ErrorEvent().Subscribe(
      [&](auto const&) { aether_app->Exit(1); });

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
