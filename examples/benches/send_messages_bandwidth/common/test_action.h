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

#ifndef EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_TEST_ACTION_H_
#define EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_TEST_ACTION_H_

#include <cstdint>
#include <utility>
#include <vector>

#include "aether/ptr/ptr.h"
#include "aether/state_machine.h"
#include "aether/actions/action.h"
#include "aether/actions/action_context.h"
#include "aether/events/event_subscription.h"

#include "aether/tele/tele.h"

#include "send_messages_bandwidth/common/bandwidth.h"

namespace ae::bench {
template <typename TAgent>
class TestAction : public Action<TestAction<TAgent>> {
  using SelfAction = Action<TestAction<TAgent>>;

  enum class State : std::uint8_t {
    kConnect,
    kHandshake,
    kWarmup,
    kSyncOneByte,
    kOneByte,
    kSyncTenBytes,
    kTenBytes,
    kSyncHundredBytes,
    kHundredBytes,
    kSyncThousandBytes,
    kThousandBytes,
    kSyncVariableSize,
    kVariableSize,
    kDone,
    kError,
  };

 public:
  explicit TestAction(ActionContext action_context, Ptr<TAgent> agent,
                      std::size_t test_message_count)
      : SelfAction{action_context},
        agent_{std::move(agent)},
        test_message_count_{test_message_count},
        state_{State::kConnect},
        state_changed_{state_.changed_event().Subscribe(
            [this](auto) { SelfAction::Trigger(); })},
        error_subscription_{agent_->error_event().Subscribe(
            [this]() { state_ = State::kError; })} {}

  ~TestAction() override { AE_TELED_DEBUG("TestAction::~TestAction"); }

  TimePoint Update(TimePoint current_time) override {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kConnect:
          Connect(current_time);
          break;
        case State::kHandshake:
          Handshake(current_time);
          break;
        case State::kWarmup:
          WarmUp(current_time);
          break;
        case State::kSyncOneByte:
          Sync(current_time, State::kOneByte);
          break;
        case State::kOneByte:
          OneByte(current_time);
          break;
        case State::kSyncTenBytes:
          Sync(current_time, State::kTenBytes);
          break;
        case State::kTenBytes:
          TenBytes(current_time);
          break;
        case State::kSyncHundredBytes:
          Sync(current_time, State::kHundredBytes);
          break;
        case State::kHundredBytes:
          HundredBytes(current_time);
          break;
        case State::kSyncThousandBytes:
          Sync(current_time, State::kThousandBytes);
          break;
        case State::kThousandBytes:
          ThousandBytes(current_time);
          break;
        case State::kSyncVariableSize:
          Sync(current_time, State::kVariableSize);
          break;
        case State::kVariableSize:
          VariableSize(current_time);
          break;
        case State::kDone:
          SelfAction::Result(*this);
          break;
        case State::kError:
          SelfAction::Error(*this);
          break;
      }
    }
    return current_time;
  }

  auto result_table() const { return result_table_; }

 private:
  void Connect(TimePoint /* current_time */) {
    agent_->Connect();
    state_ = State::kHandshake;
  }

  void Handshake(TimePoint /* current_time */) {
    AE_TELED_DEBUG("Handshake");
    handshake_subscription_ =
        agent_->Handshake().Subscribe([this]() { state_ = State::kWarmup; });
  }

  void Sync(TimePoint /* current_time */, State next_state) {
    AE_TELED_DEBUG("Sync before state {}", next_state);
    sync_subscription_ =
        agent_->Sync().Subscribe([this, next_state]() { state_ = next_state; });
  }

  void WarmUp(TimePoint /* current_time */) {
    AE_TELED_DEBUG("WarmUp");
    test_result_subscription_ = agent_->WarmUp(1000).Subscribe(
        [this](auto const&) { state_ = State::kSyncOneByte; });
  }

  void OneByte(TimePoint /* current_time */) {
    AE_TELED_DEBUG("OneByte");
    test_result_subscription_ =
        agent_->OneByte(test_message_count_)
            .Subscribe([this](auto const& bandwidth) {
              AE_TELED_DEBUG("OneByte res: {}", bandwidth);
              result_table_.push_back(bandwidth);
              state_ = State::kSyncTenBytes;
            });
  }

  void TenBytes(TimePoint /* current_time */) {
    AE_TELED_DEBUG("TenBytes");
    test_result_subscription_ =
        agent_->TenBytes(test_message_count_)
            .Subscribe([this](auto const& bandwidth) {
              AE_TELED_DEBUG("TenBytes res: {}", bandwidth);
              result_table_.push_back(bandwidth);
              state_ = State::kSyncHundredBytes;
            });
  }

  void HundredBytes(TimePoint /* current_time */) {
    AE_TELED_DEBUG("HundredBytes");

    test_result_subscription_ =
        agent_->HundredBytes(test_message_count_)
            .Subscribe([this](auto const& bandwidth) {
              AE_TELED_DEBUG("HundredBytes res: {}", bandwidth);
              result_table_.push_back(bandwidth);
              state_ = State::kSyncThousandBytes;
            });
  }

  void ThousandBytes(TimePoint /* current_time */) {
    AE_TELED_DEBUG("ThousandBytes");
    test_result_subscription_ =
        agent_->ThousandBytes(test_message_count_)
            .Subscribe([this](auto const& bandwidth) {
              AE_TELED_DEBUG("ThousandBytes res: {}", bandwidth);
              result_table_.push_back(bandwidth);
              // state_ = State::kSyncVariableSize;
              state_ = State::kDone;
            });
  }

  void VariableSize(TimePoint /* current_time */) {
    /* test_result_subscription_ =
        agent_->VariableSize().Subscribe([this](auto const& bandwidth) {
          AE_TELED_DEBUG("VariableSize res: {}", bandwidth);
          result_table_.push_back(bandwidth);
          state_ = State::kDone;
        }); */
  }

  Ptr<TAgent> agent_;
  std::size_t test_message_count_;
  StateMachine<State> state_;

  std::vector<Bandwidth> result_table_;

  Subscription state_changed_;
  Subscription error_subscription_;
  Subscription sync_subscription_;
  Subscription handshake_subscription_;
  Subscription test_result_subscription_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_TEST_ACTION_H_
