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

#include <fstream>
#include <iostream>
#include <filesystem>

#include "aether/all.h"

#include "send_message_delays/receiver.h"
#include "send_message_delays/sender.h"
#include "send_message_delays/statistics_write.h"
#include "send_message_delays/send_message_delays_manager.h"

namespace ae::bench {
static constexpr auto kTestUid =
    Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4");

class TestSendMessageDelays {
 public:
  using TestFinishedEvent = Event<void(std::optional<Result<Ignore, int>>)>;

  TestSendMessageDelays(RcPtr<AetherApp> const& aether_app,
                        std::ostream& write_results_stream)
      : ae_context_{*aether_app},
        aether_{aether_app->aether()},
        write_results_stream_{write_results_stream} {
    TestPipeline();
  }

  TestFinishedEvent::Subscriber test_finished() {
    return EventSubscriber{test_finished_event_};
  }

 private:
  auto GetClients() {
    return ex::create<ex::set_value_t(), ex::set_error_t(int)>(
        [&](auto& ctx) noexcept {
          auto selected = [&]() {
            if (client_sender_ && client_receiver_) {
              AE_TELED_INFO("All clients acquired");
              ex::set_value(std::move(ctx.receiver));
            }
          };
          auto failed = [&]() { ex::set_error(std::move(ctx.receiver), 1); };

          auto& get_sender = aether_->SelectClient(kTestUid, "sender");
          get_sender.result_event().Subscribe([&, selected](auto const& res) {
            if (res) {
              client_sender_ = res.value();
              selected();
            } else {
              failed();
            }
          });

          auto& get_receiver = aether_->SelectClient(kTestUid, "receiver");
          get_receiver.result_event().Subscribe([&, selected](auto const& res) {
            if (res) {
              client_receiver_ = res.value();
              selected();
            } else {
              failed();
            }
          });
        });
  }

  auto MakeTest() {
    return ex::create<ex::set_value_t(),
                      ex::set_error_t(int)>([&](auto& ctx) noexcept {
      AE_TELED_INFO("Make a test");
      SafeStreamConfig safe_stream_config{
          .window_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
          .max_packet_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
          .max_repeat_count = 10,
          .wait_ack_timeout = std::chrono::milliseconds{1500},
          .send_ack_timeout = std::chrono::milliseconds{0},
          .send_repeat_timeout = std::chrono::milliseconds{200},
      };

      auto sender =
          make_unique<Sender>(ae_context_, client_sender_,
                              client_receiver_->uid(), safe_stream_config);
      auto receiver = make_unique<Receiver>(ae_context_, client_receiver_,
                                            safe_stream_config);

      send_message_delays_manager_ = make_unique<SendMessageDelaysManager>(
          ae_context_, std::move(sender), std::move(receiver));

      static constexpr auto test_message_count = 100;
      auto& test_action = send_message_delays_manager_->Test({
          /* WarUp message count */ 10,
          /* test message count */ test_message_count,
      });

      test_action.result_event().Subscribe(
          [&](Result<std::vector<DurationStatistics> const&, int> const& res) {
            if (res) {
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
              auto const& results = res.value();

              std::vector<std::pair<std::string, DurationStatistics>>
                  statistics_write_list;
              statistics_write_list.reserve(res_name_table.size());
              for (std::size_t i = 0; i < res_name_table.size(); ++i) {
                statistics_write_list.emplace_back(
                    std::string{res_name_table[i]}, std::move(results[i]));
              }
              Format(write_results_stream_, "{}",
                     StatisticsWriteCsv{std::move(statistics_write_list),
                                        test_message_count});

              ex::set_value(std::move(ctx.receiver));
            } else {
              AE_TELED_ERROR("Test failed");
              ex::set_error(std::move(ctx.receiver), 2);
            }
          });
    });
  }

  void TestPipeline() {
    waiter_.emplace(
        ae_context_,
        GetClients() | ex::let_value([&]() noexcept { return MakeTest(); }),
        [&](auto&& res) {
          test_finished_event_.Emit(std::forward<decltype(res)>(res));
        });
  }

  AeContext ae_context_;
  Aether::ptr aether_;
  std::ostream& write_results_stream_;
  Client::ptr client_sender_;
  Client::ptr client_receiver_;
  std::unique_ptr<SendMessageDelaysManager> send_message_delays_manager_;

  std::optional<ex::AnyWaiter<ex::set_value_t(), ex::set_error_t(int)>> waiter_;
  TestFinishedEvent test_finished_event_;
};

int test_send_message_delays(std::ostream& result_stream) {
  auto aether_app = AetherApp::Construct(AetherAppContext{});

  auto test = TestSendMessageDelays{aether_app, result_stream};
  test.test_finished().Subscribe([&](auto res) {
    if (res && res->IsOk()) {
      aether_app->Exit(0);
    } else {
      aether_app->Exit(1);
    }
  });

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
