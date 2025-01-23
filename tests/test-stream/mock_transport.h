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

#ifndef TESTS_TEST_TRANSPORT_MOCK_TRANSPORT_H_
#define TESTS_TEST_TRANSPORT_MOCK_TRANSPORT_H_

#include "aether/events/events.h"
#include "aether/actions/action_list.h"
#include "aether/actions/action_context.h"
#include "aether/transport/actions/channel_connection_action.h"
#include "aether/transport/itransport.h"

namespace ae {
class MocTransportPacketSendAction : public PacketSendAction {
 public:
  MocTransportPacketSendAction(ActionContext action_context, DataBuffer data,
                               TimePoint sent_time);
  TimePoint Update(TimePoint current_time) override;
  void Stop() override;

  void SetState(PacketSendAction::State state);

  DataBuffer data;
  TimePoint sent_time;
};

class MockTransport : public ITransport {
 public:
  enum class ConnectAnswer {
    kAllowed,
    kDenied,
    kNoAnswer,
  };

  MockTransport(ActionContext action_context, ConnectionInfo connection_info);

  void Connect() override;

  ConnectionInfo const& GetConnectionInfo() const override;

  ConnectionSuccessEvent::Subscriber ConnectionSuccess() override;
  ConnectionErrorEvent::Subscriber ConnectionError() override;

  DataReceiveEvent::Subscriber ReceiveEvent() override;

  ActionView<PacketSendAction> Send(DataBuffer data,
                                    TimePoint current_time) override;

  /**
   * \brief Receive data sended through Send
   */
  EventSubscriber<void(MocTransportPacketSendAction& action)> sent_data_event();

  /**
   * \brief Send data to get it from receive callback after
   */
  void SendDataBack(DataBuffer data,
                    TimePoint current_time = TimePoint::clock::now());

  EventSubscriber<void(ConnectAnswer& answer)> connect_call_event();

  // Emit connected event
  void Connected();

  // Emit connection error
  void Disconnected();

 private:
  ActionList<MocTransportPacketSendAction> action_list_;
  ConnectionInfo connection_info_;

  ConnectionSuccessEvent connection_success_event_;
  ConnectionErrorEvent connection_error_event_;

  DataReceiveEvent receive_event_;
  Event<void(MocTransportPacketSendAction& action)> send_data_event_;
  Event<void(ConnectAnswer& answer)> connect_called_event_;
};
}  // namespace ae

#endif  // TESTS_TEST_TRANSPORT_MOCK_TRANSPORT_H_
