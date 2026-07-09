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

#include <iostream>
#include <string_view>

#include "aether/all.h"

static constexpr auto kParentUid =
    ae::Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4");

class TimeSynchronizer {
 public:
  TimeSynchronizer() = default;

  void SetPingSentTime(ae::TimePoint ping_sent_time);
  void SetPongSentTime(ae::TimePoint pong_sent_time);

  ae::Duration GetPingDuration() const;
  ae::Duration GetPongDuration() const;

 private:
  ae::TimePoint ping_sent_time_;
  ae::TimePoint pong_sent_time_;
};

// Alice sends "ping"s to Bob
/* class Alice {
 public:
  explicit Alice(ae::AetherApp& aether_app, ae::Client::ptr client_alice,
                 TimeSynchronizer& time_synchronizer, ae::Uid bobs_uid);

 private:
  void SendMessage();
  void ResponseReceived(ae::DataBuffer const& data_buffer);

  ae::AetherApp* aether_app_;
  ae::Client::ptr client_alice_;
  TimeSynchronizer* time_synchronizer_;
  ae::P2pStream p2pstream_;
  ae::RepeatableTask<ae::AeContext> interval_sender_;
  ae::Subscription receive_data_sub_;
  ae::MultiSubscription send_subs_;
};*/

// Bob answers "pong" to each "ping"
class Bob {
 public:
  explicit Bob(ae::AetherApp& aether_app, ae::Client::ptr client_bob,
               TimeSynchronizer& time_synchronizer);

 private:
  void OnNewStream(ae::P2pPortHandle p2p_port);
  void OnMessageReceived(ae::DataBuffer const& data_buffer);

  ae::AetherApp* aether_app_;
  ae::Client::ptr client_bob_;
  TimeSynchronizer* time_synchronizer_;
  std::unique_ptr<ae::P2pStream> p2pstream_;
  ae::Subscription new_stream_receive_sub_;
  ae::Subscription message_receive_sub_;
};

int main() {
  auto aether_app = ae::AetherApp::Construct(ae::AetherAppContext{});

  //std::unique_ptr<Alice> alice;
  std::unique_ptr<Bob> bob;
  TimeSynchronizer time_synchronizer;

  // register or load clients
  auto& bob_select = aether_app->aether()->SelectClient(kParentUid, "Bob");
  bob_select.result_event().Subscribe([&](auto const& bob_res) {
    if (bob_res) {
      bob =
          ae::make_unique<Bob>(*aether_app, bob_res.value(), time_synchronizer);
      /* auto& alice_select =
          aether_app->aether()->SelectClient(kParentUid, "Alice");
      alice_select.result_event().Subscribe(
          [&, uid = bob_res.value()->uid()](auto const& alice_res) {
            if (alice_res) {
              alice = ae::make_unique<Alice>(*aether_app, alice_res.value(),
                                             time_synchronizer, uid);
              // Save the current aether state
              aether_app->aether().Save();
            } else {
              aether_app->Exit(1);
            }
          });*/
    } else {
      aether_app->Exit(1);
    }
  });

  while (!aether_app->IsExited()) {
    auto next_time = aether_app->Update(ae::Now());
    aether_app->WaitUntil(next_time);
  }
  return aether_app->ExitCode();
}

void TimeSynchronizer::SetPingSentTime(ae::TimePoint ping_sent_time) {
  ping_sent_time_ = ping_sent_time;
}
void TimeSynchronizer::SetPongSentTime(ae::TimePoint pong_sent_time) {
  pong_sent_time_ = pong_sent_time;
}
ae::Duration TimeSynchronizer::GetPingDuration() const {
  return std::chrono::duration_cast<ae::Duration>(ae::Now() - ping_sent_time_);
}
ae::Duration TimeSynchronizer::GetPongDuration() const {
  return std::chrono::duration_cast<ae::Duration>(ae::Now() - pong_sent_time_);
}

/* Alice::Alice(ae::AetherApp& aether_app, ae::Client::ptr client_alice,
             TimeSynchronizer& time_synchronizer, ae::Uid bobs_uid)
    : aether_app_{&aether_app},
      client_alice_{std::move(client_alice)},
      time_synchronizer_{&time_synchronizer},
      p2pstream_{*aether_app_, client_alice_.Load(), bobs_uid,
                 client_alice_->message_stream_manager().CreatePort(bobs_uid)},
      interval_sender_{*aether_app_, [this]() { SendMessage(); },
                       std::chrono::seconds{5}},
      receive_data_sub_{p2pstream_.out_data_event().Subscribe(
          ae::MethodPtr<&Alice::ResponseReceived>{this})} {}

void Alice::SendMessage() {
  auto current_time = ae::Now();
  constexpr std::string_view ping_message = "ping";

  time_synchronizer_->SetPingSentTime(current_time);

  std::cout << ae::Format("[{:%H:%M:%S}] Alice sends \"ping\"'\n", ae::Now());
  p2pstream_.Write({std::begin(ping_message), std::end(ping_message)});
}

void Alice::ResponseReceived(ae::DataBuffer const& data_buffer) {
  auto pong_message = std::string_view{
      reinterpret_cast<char const*>(data_buffer.data()), data_buffer.size()};
  std::cout << ae::Format(
      "[{:%H:%M:%S}] Alice received \"{}\" within time {} ms\n", ae::Now(),
      pong_message,
      std::chrono::duration_cast<std::chrono::milliseconds>(
          time_synchronizer_->GetPongDuration())
          .count());
}*/

Bob::Bob(ae::AetherApp& aether_app, ae::Client::ptr client_bob,
         TimeSynchronizer& time_synchronizer)
    : aether_app_{&aether_app},
      client_bob_{std::move(client_bob)},
      time_synchronizer_{&time_synchronizer},
      new_stream_receive_sub_{
          client_bob_->message_stream_manager().new_port_event().Subscribe(
              ae::MethodPtr<&Bob::OnNewStream>{this})} {}

void Bob::OnNewStream(ae::P2pPortHandle p2p_port) {
  p2pstream_ = std::make_unique<ae::P2pStream>(*aether_app_, client_bob_.Load(),
                                               p2p_port.destination(),
                                               std::move(p2p_port));
  message_receive_sub_ = p2pstream_->out_data_event().Subscribe(
      ae::MethodPtr<&Bob::OnMessageReceived>{this});
}

void Bob::OnMessageReceived(ae::DataBuffer const& data_buffer) {
  auto ping_message = std::string_view{
      reinterpret_cast<char const*>(data_buffer.data()), data_buffer.size()};
  std::cout << ae::Format(
      "[{:%H:%M:%S}] Bob received \"{}\" within time {} ms\n", ae::Now(),
      ping_message,
      std::chrono::duration_cast<std::chrono::milliseconds>(
          time_synchronizer_->GetPingDuration())
          .count());
  std::cout << ae::Format("Hex string [{}]\n", data_buffer);
  time_synchronizer_->SetPongSentTime(ae::Now());
  constexpr std::string_view pong_message = "pong";
  //std::cout << ae::Format("[{:%H:%M:%S}] Bob sends \"pong\"\n", ae::Now());
  //p2pstream_->Write({std::begin(pong_message), std::end(pong_message)});
}
