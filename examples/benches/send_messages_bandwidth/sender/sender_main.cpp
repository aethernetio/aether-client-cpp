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

#include "aether/all.h"

#include "send_messages_bandwidth/sender/sender.h"
#include "send_messages_bandwidth/common/test_action.h"

#include "aether/tele/tele.h"

namespace ae::bench {
int test_sender_bandwidth(Uid receiver_uid) {
  auto aether_app = ae::AetherApp::Construct(AetherAppContext{});

  ae::Client::ptr client;

  // select one client
  auto select_client = aether_app->aether()->SelectClient(
      Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4"), 2);

  select_client->ResultEvent().Subscribe(
      [&](auto const& reg) { client = reg.client(); });

  aether_app->WaitAction(select_client);

  if (!client) {
    AE_TELED_ERROR("Registration failed");
    return -1;
  }

  auto action_context = ActionContext{*aether_app};
  auto sender = Sender{action_context, client, receiver_uid};

  auto test_action =
      ActionPtr<TestAction<Sender>>{action_context, sender, std::size_t{10000}};

  auto result_subscription =
      test_action->ResultEvent().Subscribe([&](auto const& action) {
        auto res_name_table = std::array{
            std::string_view{"1 Byte"},
            std::string_view{"10 Bytes"},
            std::string_view{"100 Bytes"},
            std::string_view{"1000 Bytes"},
        };
        auto const& results = action.result_table();

        auto res_string = std::string{};
        for (auto i = 0; i < res_name_table.size(); ++i) {
          res_string += Format("{}:{}\n", res_name_table[i], results[i]);
        }
        std::cout << "Test results: \n" << res_string << std::endl;
        aether_app->Exit(0);
      });

  auto error_subscription =
      test_action->ErrorEvent().Subscribe([&](auto const&) {
        AE_TELED_ERROR("Test failed");
        aether_app->Exit(1);
      });

  while (!aether_app->IsExited()) {
    auto time = ae::TimePoint::clock::now();
    auto next_time = aether_app->Update(time);
    aether_app->WaitUntil(std::min(next_time, time + std::chrono::seconds(5)));
  }

  return aether_app->ExitCode();
}
}  // namespace ae::bench

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <receiver_uid>\n";
    return 1;
  }
  auto uid_str = std::string(argv[1]);
  auto uid = ae::Uid::FromString(uid_str);
  return ae::bench::test_sender_bandwidth(uid);
}
