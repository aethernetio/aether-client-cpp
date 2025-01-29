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

#include "send_messages_bandwidth/common/sender.h"
#include "send_messages_bandwidth/common/test_action.h"

#include "aether/tele/tele.h"
#include "aether/tele/ios_time.h"

namespace ae::bench {

static constexpr char WIFI_SSID[] = "Test123";
static constexpr char WIFI_PASS[] = "Test123";

int test_sender_bandwidth(Uid receiver_uid) {
  auto aether_app = ae::AetherApp::Construct(
      AetherAppConstructor{}
#if defined AE_DISTILLATION
#  if AE_SUPPORT_REGISTRATION
          .RegCloud([](ae::Ptr<ae::Domain> const& domain,
                       ae::Aether::ptr const& /* aether */) {
            auto registration_cloud = domain->CreateObj<ae::RegistrationCloud>(
                ae::kRegistrationCloud);
            // localhost
            registration_cloud->AddServerSettings(ae::IpAddressPortProtocol{
                {ae::IpAddress{ae::IpAddress::Version::kIpV4, {{127, 0, 0, 1}}},
                 9010},
                ae::Protocol::kTcp});
            // cloud address
            registration_cloud->AddServerSettings(ae::IpAddressPortProtocol{
                {ae::IpAddress{ae::IpAddress::Version::kIpV4,
                               {{35, 224, 1, 127}}},
                 9010},
                ae::Protocol::kTcp});
            // cloud name address
            registration_cloud->AddServerSettings(ae::NameAddress{
                "registration.aethernet.io", 9010, ae::Protocol::kTcp});
            return registration_cloud;
          })
#  endif  // AE_SUPPORT_REGISTRATION
#endif
  );

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
  auto sender = MakePtr<Sender>(action_context, client, receiver_uid);

  auto test_action =
      TestAction<Sender>{action_context, sender, std::size_t{10000}};

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
  auto uid_arr = ae::MakeArray(uid_str);
  if (uid_arr.size() != ae::Uid::kSize) {
    std::cerr << "Uid size must be " << ae::Uid::kSize << " bytes\n";
    return 2;
  }

  auto uid = ae::Uid{};
  std::copy(std::begin(uid_arr), std::end(uid_arr), std::begin(uid.value));

  return ae::bench::test_sender_bandwidth(uid);
}
