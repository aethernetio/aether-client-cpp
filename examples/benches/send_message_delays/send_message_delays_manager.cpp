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
    ActionContext action_context, Ptr<Sender> sender, Ptr<Receiver> receiver,
    SendMessageDelaysManagerConfig config)
    : Action{action_context},
      sender_{std::move(sender)},
      receiver_{std::move(receiver)},
      config_{config},
      state_{State::kWarmUp} {
  state_changed_subscription_ =
      state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
}

TimePoint SendMessageDelaysManager::TestAction::Update(TimePoint current_time) {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kWarmUp:
        WarmUp();
        break;
      case State::kTest2Bytes:
      case State::kSsTest2Bytes:
        Test2Bytes();
        break;
      case State::kTest10Bytes:
      case State::kSsTest10Bytes:
        Test10Bytes();
        break;
      case State::kTest100Bytes:
      case State::kSsTest100Bytes:
        Test100Bytes();
        break;
      case State::kTest1000Bytes:
      case State::kSsTest1000Bytes:
        Test1000Bytes();
        break;
      case State::kTest1500Bytes:
      case State::kSsTest1500Bytes:
        Test1500Bytes();
        break;
      case State::kSwitchToSafeStream:
        SwitchToSafeStream();
        break;
      case State::kSsWarmUp:
        SafeStreamWarmUp();
        break;
      case State::kDone:
        Action::Result(*this);
        break;
      case State::kError:
        Action::Error(*this);
        break;
      case State::kStop:
        Action::Stop(*this);
        break;
    }
  }

  return current_time;
}

void SendMessageDelaysManager::TestAction::Stop() { state_.Set(State::kStop); }

std::vector<DurationStatistics> const&
SendMessageDelaysManager::TestAction::result_table() const {
  return result_table_;
}

void SendMessageDelaysManager::TestAction::WarmUp() {
  AE_TELED_INFO("WarmUp p2p stream");
  res_event_ = MakePtr<BarrierEvent<TimeTable, 2>>();

  sender_->ConnectP2pStream();
  receiver_->Connect();

  auto sender_warm_up =
      sender_->WarmUp(config_.warm_up_message_count, config_.min_send_interval);
  auto receiver_warm_up = receiver_->WarmUp(config_.warm_up_message_count);

  test_subscriptions_.Push(  //
      receiver_warm_up->OnReceived().Subscribe([sender_warm_up]() mutable {
        if (sender_warm_up) {
          sender_warm_up->Sync();
        }
      }),
      sender_warm_up->SubscribeOnResult(
          [this](auto const&) { res_event_->Emit<0>({}); }),
      receiver_warm_up->SubscribeOnResult(
          [this](auto const&) { res_event_->Emit<1>({}); }),
      sender_warm_up->SubscribeOnError([this](auto const&) {
        AE_TELED_ERROR("Warm up sender error");
        state_ = State::kError;
      }),
      receiver_warm_up->SubscribeOnError([this](auto const&) {
        AE_TELED_ERROR("Warm up receiver error");
        state_ = State::kError;
      }),
      // on success go to connection
      res_event_->Subscribe([this](auto const&) {
        AE_TELED_INFO("WarmUp p2p stream finished");
        state_.Set(State::kTest2Bytes);
      }));
}

void SendMessageDelaysManager::TestAction::SwitchToSafeStream() {
  sender_->Disconnect();
  receiver_->Disconnect();
  state_ = State::kSsWarmUp;
}

void SendMessageDelaysManager::TestAction::SafeStreamWarmUp() {
  AE_TELED_INFO("WarmUp p2p safe stream");
  res_event_ = MakePtr<BarrierEvent<TimeTable, 2>>();

  sender_->ConnectP2pSafeStream();
  receiver_->Connect();

  auto sender_warm_up =
      sender_->WarmUp(config_.warm_up_message_count, config_.min_send_interval);
  auto receiver_warm_up = receiver_->WarmUp(config_.warm_up_message_count);

  test_subscriptions_.Push(  //
      receiver_warm_up->OnReceived().Subscribe([sender_warm_up]() mutable {
        if (sender_warm_up) {
          sender_warm_up->Sync();
        }
      }),
      sender_warm_up->SubscribeOnResult(
          [this](auto const&) { res_event_->Emit<0>({}); }),
      receiver_warm_up->SubscribeOnResult(
          [this](auto const&) { res_event_->Emit<1>({}); }),
      sender_warm_up->SubscribeOnError([this](auto const&) {
        AE_TELED_ERROR("Warm up sender error");
        state_ = State::kError;
      }),
      receiver_warm_up->SubscribeOnError([this](auto const&) {
        AE_TELED_ERROR("Warm up receiver error");
        state_ = State::kError;
      }),
      // on success go to connection
      res_event_->Subscribe([this](auto const&) {
        AE_TELED_INFO("WarmUp p2p safe stream finished");
        state_.Set(State::kSsTest2Bytes);
      }));
}

void SendMessageDelaysManager::TestAction::Test2Bytes() {
  AE_TELED_INFO("Test2Bytes");

  auto sender_event = sender_->Send2Bytes(config_.test_message_count,
                                          config_.min_send_interval);
  auto receiver_event = receiver_->Receive2Bytes(config_.test_message_count);
  SubscribeToTest(sender_event, receiver_event,
                  state_.get() == State::kTest2Bytes ? State::kTest10Bytes
                                                     : State::kSsTest10Bytes);
}

void SendMessageDelaysManager::TestAction::Test10Bytes() {
  AE_TELED_INFO("Test10Bytes");

  auto sender_event = sender_->Send10Bytes(config_.test_message_count,
                                           config_.min_send_interval);
  auto receiver_event = receiver_->Receive10Bytes(config_.test_message_count);
  SubscribeToTest(sender_event, receiver_event,
                  state_.get() == State::kTest10Bytes ? State::kTest100Bytes
                                                      : State::kSsTest100Bytes);
}

void SendMessageDelaysManager::TestAction::Test100Bytes() {
  AE_TELED_INFO("Test100Bytes");

  auto sender_event = sender_->Send100Bytes(config_.test_message_count,
                                            config_.min_send_interval);
  auto receiver_event = receiver_->Receive100Bytes(config_.test_message_count);
  SubscribeToTest(sender_event, receiver_event,
                  state_.get() == State::kTest100Bytes
                      ? State::kTest1000Bytes
                      : State::kSsTest1000Bytes);
}

void SendMessageDelaysManager::TestAction::Test1000Bytes() {
  AE_TELED_INFO("Test1000Bytes");

  auto sender_event = sender_->Send1000Bytes(config_.test_message_count,
                                             config_.min_send_interval);
  auto receiver_event = receiver_->Receive1000Bytes(config_.test_message_count);
  SubscribeToTest(sender_event, receiver_event,
                  state_.get() == State::kTest1000Bytes
                      ? State::kTest1500Bytes
                      : State::kSsTest1500Bytes);
}

void SendMessageDelaysManager::TestAction::Test1500Bytes() {
  AE_TELED_INFO("Test1500Bytes");

  auto sender_event = sender_->Send1500Bytes(config_.test_message_count,
                                             config_.min_send_interval);
  auto receiver_event = receiver_->Receive1500Bytes(config_.test_message_count);
  SubscribeToTest(sender_event, receiver_event,
                  state_.get() == State::kTest1500Bytes
                      ? State::kSwitchToSafeStream
                      : State::kDone);
}

void SendMessageDelaysManager::TestAction::SubscribeToTest(
    ActionView<ITimedSender> sender_action,
    ActionView<ITimedReceiver> receiver_action, State next_state) {
  test_subscriptions_.Reset();
  res_event_ = MakePtr<BarrierEvent<TimeTable, 2>>();
  test_subscriptions_.Push(
      receiver_action->OnReceived().Subscribe([sender_action]() mutable {
        if (sender_action) {
          sender_action->Sync();
        }
      }),
      sender_action->SubscribeOnResult([this](auto const& action) {
        res_event_->Emit<0>(action.message_times());
      }),
      sender_action->SubscribeOnError([this](auto const&) {
        AE_TELED_ERROR("Sender error");
        state_ = State::kError;
      }),
      receiver_action->SubscribeOnResult([this](auto const& action) {
        res_event_->Emit<1>(action.message_times());
      }),
      receiver_action->SubscribeOnError([this](auto const&) {
        AE_TELED_ERROR("Receiver error");
        state_ = State::kError;
      }),
      res_event_->Subscribe(
          [this, next_state](BarrierEvent<TimeTable, 2> const& res_event) {
            AE_TELED_INFO("Test finished");
            assert(res_event.Get<0>().size() == res_event.Get<1>().size());
            TestResult(res_event.Get<0>(), res_event.Get<1>());
            state_ = next_state;
          }));
}

void SendMessageDelaysManager::TestAction::TestResult(
    TimeTable const& sended_table, TimeTable const& received_table) {
  DurationTable results;
  // 0 -is sender, 1 - is receiver
  for (auto const& [id, sended] : sended_table) {
    auto received = received_table.at(id);
    auto diff = std::chrono::duration_cast<Duration>(received - sended);
    results.emplace_back(id, diff);
  }

  result_table_.emplace_back(std::move(results));
}

SendMessageDelaysManager::SendMessageDelaysManager(ActionContext action_context,
                                                   Ptr<Sender> sender,
                                                   Ptr<Receiver> receiver)
    : action_context_{std::move(action_context)},
      sender_{std::move(sender)},
      receiver_{std::move(receiver)} {}

ActionView<SendMessageDelaysManager::TestAction> SendMessageDelaysManager::Test(
    SendMessageDelaysManagerConfig config) {
  AE_TELED_INFO("Test started");
  if (test_action_) {
    test_action_->Stop();
  }

  test_action_ =
      MakePtr<TestAction>(action_context_, sender_, receiver_, config);

  test_action_subscription_.Push(test_action_->FinishedEvent().Subscribe(
      [this]() { test_action_.Reset(); }));

  return *test_action_;
}

}  // namespace ae::bench
