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

#include "send_message_delays/send_message_delays_manager.h"

#include <utility>

#include "aether/tele/tele.h"

namespace ae::bench {
SendMessageDelaysManager::TestAction::TestAction(
    AeContext const& ae_context, Sender& sender, Receiver& receiver,
    SendMessageDelaysManagerConfig config)
    : ae_context_{ae_context},
      sender_{&sender},
      receiver_{&receiver},
      config_{config} {
  TestPipeline();
}

SendMessageDelaysManager::TestAction::ResultEvent::Subscriber
SendMessageDelaysManager::TestAction::result_event() {
  return EventSubscriber{result_event_};
}

auto SendMessageDelaysManager::TestAction::ConnectPeers() {
  return ex::then([&]() noexcept {
    sender_->ConnectP2pStream();
    receiver_->ConnectP2pStream();
  });
}

auto SendMessageDelaysManager::TestAction::SafeConnectPeers() {
  return ex::then([&]() noexcept {
    sender_->Disconnect();
    receiver_->Disconnect();

    AE_TELED_INFO("Switch to safe stream");
    sender_->ConnectP2pSafeStream();
    receiver_->ConnectP2pSafeStream();
  });
}

auto SendMessageDelaysManager::TestAction::WarmUp() {
  return ex::let_value([&]() noexcept {
    return ex::create<ex::set_value_t(), ex::set_error_t(int)>(
        [&](auto& ctx) noexcept {
          AE_TELED_INFO("WarmUp");
          res_event_ = make_unique<CumulativeEvent<TimeTable, 2>>();

          auto& sender_warm_up = sender_->WarmUp();
          auto& receiver_warm_up =
              receiver_->WarmUp(config_.warm_up_message_count);

          res_event_->Connect([&](auto, auto const&) {},
                              sender_warm_up.message_times_event(),
                              receiver_warm_up.message_times_event());

          test_subscriptions_.Push(  //
              receiver_warm_up.on_timeout().Subscribe([&]() {
                AE_TELED_ERROR("Warm up error");
                ex::set_error(std::move(ctx.receiver), 1);
              }),
              receiver_warm_up.on_received().Subscribe(
                  [&sender_warm_up](bool last) mutable {
                    if (last) {
                      sender_warm_up.Stop();
                    } else {
                      sender_warm_up.Sync();
                    }
                  }),
              // on success go to tests
              res_event_->Subscribe([&](auto const&) {
                AE_TELED_INFO("WarmUp p2p stream finished");
                ex::set_value(std::move(ctx.receiver));
              }));
        });
  });
}

auto SendMessageDelaysManager::TestAction::SubscribeToTest(
    TimedSender& sender_action, TimedReceiver& receiver_action) {
  return ex::create<ex::set_value_t(), ex::set_error_t(int)>(
      [&](auto& ctx) noexcept {
        test_subscriptions_.Reset();
        res_event_ = make_unique<CumulativeEvent<TimeTable, 2>>();
        res_event_->Connect(
            [&](auto times_setter, auto const& message_times) {
              times_setter = message_times;
            },
            sender_action.message_times_event(),
            receiver_action.message_times_event());

        test_subscriptions_.Push(
            receiver_action.on_received().Subscribe([&](bool last) mutable {
              if (!last) {
                sender_action.Sync();
              } else {
                sender_action.Stop();
              }
            }),
            receiver_action.on_timeout().Subscribe([&]() {
              AE_TELED_ERROR("Test error");
              ex::set_error(std::move(ctx.receiver), 2);
            }),
            res_event_->Subscribe([&](auto const& res_event) {
              AE_TELED_INFO("Test finished");
              TestResult(res_event[0], res_event[1]);
              ex::set_value(std::move(ctx.receiver));
            }));
      });
}

auto SendMessageDelaysManager::TestAction::Test2Bytes() {
  return ex::let_value([&]() noexcept {
    AE_TELED_INFO("Test2Bytes");
    auto& sender_action = sender_->Send2Bytes();
    auto& receiver_action =
        receiver_->Receive2Bytes(config_.test_message_count);
    return SubscribeToTest(sender_action, receiver_action);
  });
}

auto SendMessageDelaysManager::TestAction::Test10Bytes() {
  return ex::let_value([&]() noexcept {
    AE_TELED_INFO("Test10Bytes");
    auto& sender_action = sender_->Send10Bytes();
    auto& receiver_action =
        receiver_->Receive10Bytes(config_.test_message_count);
    return SubscribeToTest(sender_action, receiver_action);
  });
}

auto SendMessageDelaysManager::TestAction::Test100Bytes() {
  return ex::let_value([&]() noexcept {
    AE_TELED_INFO("Test100Bytes");

    auto& sender_action = sender_->Send100Bytes();
    auto& receiver_action =
        receiver_->Receive100Bytes(config_.test_message_count);
    return SubscribeToTest(sender_action, receiver_action);
  });
}

auto SendMessageDelaysManager::TestAction::Test1000Bytes() {
  return ex::let_value([&]() noexcept {
    AE_TELED_INFO("Test1000Bytes");

    auto& sender_action = sender_->Send1000Bytes();
    auto& receiver_action =
        receiver_->Receive1000Bytes(config_.test_message_count);
    return SubscribeToTest(sender_action, receiver_action);
  });
}

void SendMessageDelaysManager::TestAction::TestPipeline() {
  auto test_send_bytes = WarmUp() | Test2Bytes() | Test10Bytes() |
                         Test100Bytes() | Test1000Bytes();

  auto pipeline = ex::just() | ConnectPeers() | test_send_bytes |
                  SafeConnectPeers() | test_send_bytes;

  test_pipeline_ =
      std::make_unique<ex::AnyWaiter<ex::set_value_t(), ex::set_error_t(int)>>(
          ae_context_, std::move(pipeline),
          [&](std::optional<Result<Ignore, int>> res) {
            if (res && res->IsOk()) {
              result_event_.Emit(
                  Ok<std::vector<DurationStatistics> const&>{result_table_});
            } else {
              result_event_.Emit(Error{-1});
            }
          });
}

void SendMessageDelaysManager::TestAction::TestResult(
    TimeTable const& sent_table, TimeTable const& received_table) {
  DurationTable results;
  // 0 -is sender, 1 - is receiver
  for (auto const& [id, sent] : sent_table) {
    auto received = received_table.find(id);
    if (received == std::end(received_table)) {
      results.emplace_back(id, std::nullopt);
      continue;
    }
    auto diff = std::chrono::duration_cast<Duration>(received->second - sent);
    results.emplace_back(id, diff);
  }

  result_table_.emplace_back(std::move(results));
}

SendMessageDelaysManager::SendMessageDelaysManager(
    AeContext const& ae_context, std::unique_ptr<Sender> sender,
    std::unique_ptr<Receiver> receiver)
    : ae_context_{ae_context},
      sender_{std::move(sender)},
      receiver_{std::move(receiver)} {}

SendMessageDelaysManager::TestAction& SendMessageDelaysManager::Test(
    SendMessageDelaysManagerConfig config) {
  AE_TELED_INFO("Test started");
  test_action_ =
      std::make_unique<TestAction>(ae_context_, *sender_, *receiver_, config);

  return *test_action_;
}

}  // namespace ae::bench
