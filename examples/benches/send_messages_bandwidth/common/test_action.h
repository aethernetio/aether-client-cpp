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
#include <vector>

#include "aether-miscpp/types/result.h"
#include "aether/ae_context.h"
#include "aether/events/event_subscription.h"
#include "aether/events/events.h"
#include "aether/executors/executors.h"

#include "send_messages_bandwidth/common/bandwidth.h"

#include "aether/tele.h"

namespace ae::bench {
template <typename TAgent>
class TestAction {
 public:
  using ResultEvent = Event<void(Result<std::vector<Bandwidth> const&, int>)>;

  TestAction(AeContext const& ae_context, TAgent& agent,
             std::size_t test_message_count)
      : ae_context_{ae_context},
        agent_{&agent},
        test_message_count_{test_message_count} {
    TestPipeline();
  }

  ResultEvent::Subscriber result_event() {
    return EventSubscriber{result_event_};
  }

 private:
  auto Connect() {
    agent_->Connect();
    return ex::just();
  }
  auto Handshake() {
    return ex::let_value([&]() noexcept {
      AE_TELED_DEBUG("Handshake");
      return ex::create<ex::set_value_t(), ex::set_error_t(int)>(
          [&](auto& ctx) noexcept {
            res_sub_ = agent_->Handshake().Subscribe(
                [&]() { ex::set_value(std::move(ctx.receiver)); });
            error_sub_ = agent_->error_event().Subscribe(
                [&]() { ex::set_error(std::move(ctx.receiver), 1); });
          });
    });
  }

  auto WarmUp() {
    return ex::let_value([&]() noexcept {
      AE_TELED_DEBUG("WarmUp");
      return ex::create<ex::set_value_t(), ex::set_error_t(int)>(
          [&](auto& ctx) noexcept {
            res_sub_ = agent_->TestMessages(100, 1).Subscribe(
                [&](auto const&) { ex::set_value(std::move(ctx.receiver)); });
            error_sub_ = agent_->error_event().Subscribe(
                [&]() { ex::set_error(std::move(ctx.receiver), 1); });
          });
    });
  }

  template <std::size_t Count>
  auto TestSendBytes() {
    return ex::let_value([&]() noexcept {
      AE_TELED_DEBUG("Test send bytes {}", Count);

      return ex::create<ex::set_value_t(), ex::set_error_t(int)>(
          [&](auto& ctx) noexcept {
            res_sub_ =
                agent_->TestMessages(test_message_count_, Count)
                    .Subscribe([&](auto const& bandwidth) {
                      AE_TELED_DEBUG("Test {} res: {}", Count, bandwidth);
                      result_table_.push_back(bandwidth);
                      ex::set_value(std::move(ctx.receiver));
                    });
            error_sub_ = agent_->error_event().Subscribe(
                [&]() { ex::set_error(std::move(ctx.receiver), 1); });
          });
    });
  }

  void TestPipeline() {
    auto s = Connect() | Handshake() | WarmUp() | TestSendBytes<1>() |
             TestSendBytes<10>() | TestSendBytes<100>() | TestSendBytes<1000>();

    test_pipeline_.emplace(ae_context_, std::move(s), [this](auto const& res) {
      if (res && res->IsOk()) {
        result_event_.Emit(Ok<std::vector<Bandwidth> const&>(result_table_));
      } else {
        result_event_.Emit(Error{res.value_or(Error{-1}).error()});
      }
    });
  }

  AeContext ae_context_;
  TAgent* agent_;
  std::size_t test_message_count_;
  std::optional<ex::AnyWaiter<ex::set_value_t(), ex::set_error_t(int)>>
      test_pipeline_;

  std::vector<Bandwidth> result_table_;
  ResultEvent result_event_;

  Subscription res_sub_;
  Subscription error_sub_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGES_BANDWIDTH_COMMON_TEST_ACTION_H_
