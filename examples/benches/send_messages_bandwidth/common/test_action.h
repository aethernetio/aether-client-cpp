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
  explicit TestAction(ActionContext action_context, TAgent& agent,
                      std::size_t test_message_count)
      : SelfAction{action_context},
        agent_{&agent},
        test_message_count_{test_message_count},
        state_{State::kConnect},
        state_changed_{state_.changed_event().Subscribe(
            [this](auto) { SelfAction::Trigger(); })},
        error_subscription_{agent_->error_event().Subscribe(
            [this]() { state_ = State::kError; })} {}

  ~TestAction() override { AE_TELED_DEBUG("TestAction::~TestAction"); }

  ActionResult Update() {
    if (state_.changed()) {
      switch (state_.Acquire()) {
        case State::kConnect:
          Connect();
          break;
        case State::kHandshake:
          Handshake();
          break;
        case State::kWarmup:
          WarmUp();
          break;
        case State::kSyncOneByte:
          Sync(State::kOneByte);
          break;
        case State::kOneByte:
          OneByte();
          break;
        case State::kSyncTenBytes:
          Sync(State::kTenBytes);
          break;
        case State::kTenBytes:
          TenBytes();
          break;
        case State::kSyncHundredBytes:
          Sync(State::kHundredBytes);
          break;
        case State::kHundredBytes:
          HundredBytes();
          break;
        case State::kSyncThousandBytes:
          Sync(State::kThousandBytes);
          break;
        case State::kThousandBytes:
          ThousandBytes();
          break;
        case State::kSyncVariableSize:
          Sync(State::kVariableSize);
          break;
        case State::kVariableSize:
          VariableSize();
          break;
        case State::kDone:
          return ActionResult::Result();
        case State::kError:
          return ActionResult::Error();
      }
    }
    return {};
  }

  auto result_table() const { return result_table_; }

 private:
  void Connect() {
    agent_->Connect();
    state_ = State::kHandshake;
  }

  void Handshake() {
    AE_TELED_DEBUG("Handshake");
    handshake_subscription_ =
        agent_->Handshake().Subscribe([this]() { state_ = State::kWarmup; });
  }

  void Sync(State next_state) {
    AE_TELED_DEBUG("Sync before state {}", next_state);
    sync_subscription_ =
        agent_->Sync().Subscribe([this, next_state]() { state_ = next_state; });
  }

  void WarmUp() {
    AE_TELED_DEBUG("WarmUp");
    test_result_subscription_ = agent_->WarmUp(1000).Subscribe(
        [this](auto const&) { state_ = State::kSyncOneByte; });
  }

  void OneByte() {
    AE_TELED_DEBUG("OneByte");
    test_result_subscription_ =
        agent_->OneByte(test_message_count_)
            .Subscribe([this](auto const& bandwidth) {
              AE_TELED_DEBUG("OneByte res: {}", bandwidth);
              result_table_.push_back(bandwidth);
              state_ = State::kSyncTenBytes;
            });
  }

  void TenBytes() {
    AE_TELED_DEBUG("TenBytes");
    test_result_subscription_ =
        agent_->TenBytes(test_message_count_)
            .Subscribe([this](auto const& bandwidth) {
              AE_TELED_DEBUG("TenBytes res: {}", bandwidth);
              result_table_.push_back(bandwidth);
              state_ = State::kSyncHundredBytes;
            });
  }

  void HundredBytes() {
    AE_TELED_DEBUG("HundredBytes");

    test_result_subscription_ =
        agent_->HundredBytes(test_message_count_)
            .Subscribe([this](auto const& bandwidth) {
              AE_TELED_DEBUG("HundredBytes res: {}", bandwidth);
              result_table_.push_back(bandwidth);
              state_ = State::kSyncThousandBytes;
            });
  }

  void ThousandBytes() {
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

  void VariableSize() {
    /* test_result_subscription_ =
        agent_->VariableSize().Subscribe([this](auto const& bandwidth) {
          AE_TELED_DEBUG("VariableSize res: {}", bandwidth);
          result_table_.push_back(bandwidth);
          state_ = State::kDone;
        }); */
  }

  TAgent* agent_;
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
