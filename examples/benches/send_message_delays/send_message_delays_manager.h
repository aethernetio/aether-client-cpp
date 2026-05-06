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

#include "aether/clock.h"
#include "aether/types/result.h"
#include "aether/executors/executors.h"
#include "aether/events/cumulative_event.h"
#include "aether/events/multi_subscription.h"

#include "send_message_delays/sender.h"
#include "send_message_delays/receiver.h"
#include "send_message_delays/delay_statistics.h"

namespace ae::bench {

struct SendMessageDelaysManagerConfig {
  std::size_t warm_up_message_count;
  std::size_t test_message_count;
};

class SendMessageDelaysManager {
  class TestAction {
   public:
    using ResultEvent =
        Event<void(Result<std::vector<DurationStatistics> const&, int>)>;

    TestAction(AeContext const& ae_context, Sender& sender, Receiver& receiver,
               SendMessageDelaysManagerConfig config);

    ResultEvent::Subscriber result_event();

   private:
    auto ConnectPeers();
    auto SafeConnectPeers();
    auto WarmUp();
    auto SwitchToSafeStream();
    auto SafeStreamWarmUp();
    auto Test2Bytes();
    auto Test10Bytes();
    auto Test100Bytes();
    auto Test1000Bytes();

    auto SubscribeToTest(TimedSender& sender_action,
                         TimedReceiver& receiver_action);

    void TestResult(TimeTable const& sent_table,
                    TimeTable const& received_table);

    void TestPipeline();

    AeContext ae_context_;
    Sender* sender_;
    Receiver* receiver_;
    SendMessageDelaysManagerConfig config_;

    std::unique_ptr<CumulativeEvent<TimeTable, 2>> res_event_;
    MultiSubscription test_subscriptions_;
    std::unique_ptr<ex::AnyWaiter<ex::set_value_t(), ex::set_error_t(int)>>
        test_pipeline_;

    std::vector<DurationStatistics> result_table_;
    ResultEvent result_event_;
  };

 public:
  SendMessageDelaysManager(AeContext const& ae_context,
                           std::unique_ptr<Sender> sender,
                           std::unique_ptr<Receiver> receiver);

  TestAction& Test(SendMessageDelaysManagerConfig config);

 private:
  AeContext ae_context_;
  std::unique_ptr<Sender> sender_;
  std::unique_ptr<Receiver> receiver_;

  std::unique_ptr<TestAction> test_action_;
};
}  // namespace ae::bench

#endif  // EXAMPLES_BENCHES_SEND_MESSAGE_DELAYS_SEND_MESSAGE_DELAYS_MANAGER_H_
