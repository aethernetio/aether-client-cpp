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

#include "aether/obj/domain.h"
#include "aether/port/tele_init.h"
#include "aether/literal_array.h"

#include "aether/aether.h"
#include "aether/client.h"
#include "aether/global_ids.h"
#include "aether/aether_app.h"

#include "send_messages_bandwidth/common/receiver.h"
#include "send_messages_bandwidth/common/test_action.h"

#include "aether/tele/tele.h"

namespace ae::bench {

int test_receiver_bandwidth() {
  auto aether_app = ae::AetherApp::Construct(AetherAppConstructor{});

  ae::Client::ptr client;

  // register one client
  if (aether_app->aether()->clients().empty()) {
#if AE_SUPPORT_REGISTRATION
    auto client_register = aether_app->aether()->RegisterClient(
        ae::Uid{ae::MakeLiteralArray("3ac931653d37497087a6fa4ee27744e4")});

    bool register_done = false;
    bool register_failed = false;

    auto reg = client_register->SubscribeOnResult([&](auto const& reg) {
      register_done = true;
      client = reg.client();
    });
    auto reg_failed = client_register->SubscribeOnError(
        [&](auto const&) { register_failed = true; });

    while (!register_done && !register_failed) {
      auto next_time = aether_app->Update(ae::Now());
      aether_app->WaitUntil(next_time);
    }
    if (register_failed) {
      AE_TELED_ERROR("Registration failed");
      return -1;
    }
#endif
  } else {
    client = aether_app->aether()->clients()[0];
  }

  auto action_context = ActionContext{*aether_app->aether()->action_processor};
  auto receiver = MakePtr<Receiver>(action_context, client);

  auto test_action =
      TestAction<Receiver>(action_context, receiver, std::size_t{10000});

  auto result_subscription =
      test_action.SubscribeOnResult([&](auto const& action) {
        auto res_name_table = std::array{
            std::string_view{"1 Byte"}, std::string_view{"10 Bytes"},
            std::string_view{"100 Bytes"}, std::string_view{"1000 Bytes"},
            /* std::string_view{"1500 Bytes"}, */
        };
        auto const& results = action.result_table();

        auto res_string = std::string{};
        for (auto i = 0; i < res_name_table.size(); ++i) {
          res_string += Format("{}:{}\n", res_name_table[i], results[i]);
        }
        AE_TELED_DEBUG("Test results: \n {}", res_string);

        aether_app->Exit(0);
      });

  auto error_subscription = test_action.SubscribeOnError([&](auto const&) {
    AE_TELED_ERROR("Test failed");
    aether_app->Exit(1);
  });

  AE_TELED_INFO("Receiver prepared for test width uid {}", client->uid());

  while (!aether_app->IsExited()) {
    auto time = ae::TimePoint::clock::now();
    auto next_time = aether_app->Update(time);
    aether_app->WaitUntil(std::min(next_time, time + std::chrono::seconds(5)));
  }

  return aether_app->ExitCode();
}
}  // namespace ae::bench

int main() { return ae::bench::test_receiver_bandwidth(); }
