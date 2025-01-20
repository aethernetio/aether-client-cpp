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
#ifndef EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_SEND_MESSAGE_DELAYS_MANAGER_H_
#define EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_SEND_MESSAGE_DELAYS_MANAGER_H_

#include <vector>

#include "aether/common.h"
#include "aether/state_machine.h"
#include "aether/actions/action.h"
#include "aether/actions/action_view.h"
#include "aether/actions/action_context.h"
#include "aether/events/multi_subscription.h"
#include "aether/events/barrier_event.h"

#include "send_message_delays/receiver.h"
#include "send_message_delays/sender.h"
#include "send_message_delays/delay_statistics.h"

namespace ae::bench {

struct SendMessageDelaysManagerConfig {
  std::size_t warm_up_message_count;
  std::size_t test_message_count;
  Duration min_send_interval;
};

class SendMessageDelaysManager {
  class TestAction : public Action<TestAction> {
    enum class State : std::uint8_t {
      kWarmUp,
      kTest2Bytes,
      kTest10Bytes,
      kTest100Bytes,
      kTest1000Bytes,
      kTest1500Bytes,
      kSwitchToSafeStream,
      kSsWarmUp,
      kSsTest2Bytes,
      kSsTest10Bytes,
      kSsTest100Bytes,
      kSsTest1000Bytes,
      kSsTest1500Bytes,
      kDone,
      kError,
      kStop,
    };

   public:
    TestAction(ActionContext action_context, Ptr<Sender> sender,
               Ptr<Receiver> receiver, SendMessageDelaysManagerConfig config);

    TimePoint Update(TimePoint current_time) override;

    void Stop();

    std::vector<DurationStatistics> const& result_table() const;

   private:
    void WarmUp();
    void SwitchToSafeStream();
    void SafeStreamWarmUp();
    void Test2Bytes();
    void Test10Bytes();
    void Test100Bytes();
    void Test1000Bytes();
    void Test1500Bytes();

    void SubscribeToTest(ActionView<ITimedSender> sender_action,
                         ActionView<ITimedReceiver> receiver_action,
                         State next_state);

    void TestResult(TimeTable const& sended_table,
                    TimeTable const& received_table);

    Ptr<Sender> sender_;
    Ptr<Receiver> receiver_;
    SendMessageDelaysManagerConfig config_;

    Ptr<BarrierEvent<TimeTable, 2>> res_event_;
    StateMachine<State> state_;
    Subscription state_changed_subscription_;
    MultiSubscription error_subscriptions_;
    MultiSubscription test_subscriptions_;
    Subscription connection_subscriptions_;

    std::vector<DurationStatistics> result_table_;
  };

 public:
  SendMessageDelaysManager(ActionContext action_context, Ptr<Sender> sender,
                           Ptr<Receiver> receiver);

  ActionView<TestAction> Test(SendMessageDelaysManagerConfig config);

 private:
  ActionContext action_context_;
  Ptr<Sender> sender_;
  Ptr<Receiver> receiver_;

  Ptr<TestAction> test_action_;
  MultiSubscription test_action_subscription_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_SEND_MESSAGE_DELAYS_MANAGER_H_
