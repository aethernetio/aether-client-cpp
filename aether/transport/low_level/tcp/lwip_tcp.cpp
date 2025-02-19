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

#include "aether/transport/low_level/tcp/lwip_tcp.h"

#if defined LWIP_TCP_TRANSPORT_ENABLED
#  include <string.h>

#  include <vector>
#  include <utility>

#  include "aether/mstream_buffers.h"
#  include "aether/mstream.h"
#  include "aether/format/format.h"
#  include "aether/transport/transport_tele.h"

#  include "freertos/FreeRTOS.h"
#  include "freertos/task.h"

#  include "lwip/err.h"
#  include "lwip/sockets.h"
#  include "lwip/sys.h"
#  include "lwip/netdb.h"
#  include "lwip/netif.h"

extern "C" int lwip_hook_ip6_input(struct pbuf *p, struct netif *inp)
    __attribute__((weak));
extern "C" int lwip_hook_ip6_input(struct pbuf *p, struct netif *inp) {
  if (ip6_addr_isany_val(inp->ip6_addr[0].u_addr.ip6)) {
    // We don't have an LL address -> eat this packet here, so it won't get
    // accepted on input netif
    pbuf_free(p);
    return 1;
  }
  return 0;
}

namespace ae {

LwipTcpTransport::ConnectionAction::ConnectionAction(
    ActionContext action_context, LwipTcpTransport &transport)
    : Action{action_context},
      endpoint_{transport.endpoint_},
      state_{State::kConnecting} {
  state_changed_subscription_ =
      state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
  Connect();
}

TimePoint LwipTcpTransport::ConnectionAction::Update(TimePoint current_time) {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kConnected:
        Action::Result(*this);
        break;
      case State::kNotConnected:
        Action::Error(*this);
        break;
      default:
        break;
    }
  }
  return current_time;
}

int LwipTcpTransport::ConnectionAction::get_socket() const { return socket_; }
LwipTcpTransport::ConnectionAction::State
LwipTcpTransport::ConnectionAction::get_state() const {
  return state_.get();
}

void LwipTcpTransport::ConnectionAction::Connect() {
  struct sockaddr_in servaddr;
  timeval tv;
  socklen_t optlen;
  int on = 1;
  int res = 0;

  AE_TELE_DEBUG(TcpTransportConnect, "Connect to {}", endpoint_);

  if ((socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) < 0) {
    AE_TELED_ERROR("Socket not created");
    state_.Set(State::kNotConnected);
    return;
  }

  // set receive timeout on socket.
  tv.tv_sec = RCV_TIMEOUT_SEC;
  tv.tv_usec = RCV_TIMEOUT_USEC;
  optlen = sizeof(tv);
  res = setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &tv, optlen);
  if (res != ESP_OK) {
    AE_TELED_ERROR(
        "setupSocket(): setsockopt() SO_RCVTIMEO on client socket: error: "
        "{} {}",
        errno, strerror(errno));
    close(socket_);
    socket_ = kInvalidSocket;
    state_.Set(State::kNotConnected);
    return;
  }

  // set reuse address on socket.
  optlen = sizeof(on);
  res = setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &on, optlen);
  if (res != ESP_OK) {
    AE_TELED_ERROR(
        "setupSocket(): setsockopt() SO_REUSEADDR on client socket: error: "
        "{} {}",
        errno, strerror(errno));
    close(socket_);
    socket_ = kInvalidSocket;
    state_.Set(State::kNotConnected);
    return;
  }

  optlen = sizeof(on);
  res = setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY, &on, sizeof(on));
  if (res != ESP_OK) {
    AE_TELED_ERROR(
        "setupSocket(): setsockopt() TCP_NODELAY on client socket: error: "
        "{} {}",
        errno, strerror(errno));
    close(socket_);
    socket_ = kInvalidSocket;
    state_.Set(State::kNotConnected);
    return;
  }

  AE_TELED_DEBUG("Socket created");
  memset(&servaddr, 0, sizeof(servaddr));
  // Fill server information
  auto addr_string = ae::Format("{}", endpoint_.ip);

  servaddr.sin_family = AF_INET;  // IPv4
  servaddr.sin_addr.s_addr = inet_addr(addr_string.c_str());
  servaddr.sin_port = htons(endpoint_.port);

  if (connect(socket_, (struct sockaddr *)&servaddr,
              sizeof(struct sockaddr_in)) < 0) {
    AE_TELED_ERROR("Not connected {} {}", errno, strerror(errno));
    close(socket_);
    socket_ = kInvalidSocket;
    state_.Set(State::kNotConnected);
    return;
  }
  AE_TELED_DEBUG("Connected");
  state_.Set(State::kConnected);
}

LwipTcpTransport::LwipTcpPacketSendAction::LwipTcpPacketSendAction(
    ActionContext action_context, int socket, DataBuffer data,
    TimePoint current_time)
    : SocketPacketSendAction{action_context},
      socket_{socket},
      data_{data},
      current_time_{current_time},
      sent_offset_{0} {
  state_changed_subscription_ =
      state_.changed_event().Subscribe([this](auto) { Action::Trigger(); });
}

void LwipTcpTransport::LwipTcpPacketSendAction::Send() {
  if (state_.get() == State::kQueued) {
    state_.Set(State::kProgress);
  }

  auto size_to_send = data_.size() - sent_offset_;
  auto r = send(socket_, data_.data() + sent_offset_, size_to_send, 0);

  if ((r >= 0) && (r < size_to_send)) {
    // save remaining data to later write
    sent_offset_ += static_cast<std::size_t>(r);
    return;
  }
  if (r == -1) {
    if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
      AE_TELED_ERROR("Send to socket error {} {}", errno, strerror(errno));
      state_.Set(State::kFailed);
    }
    return;
  }
  state_.Set(State::kSuccess);
}

LwipTcpTransport::LwipTcpTransport(ActionContext action_context,
                                   IPoller::ptr poller,
                                   IpAddressPort const &endpoint)
    : action_context_{action_context},
      poller_{std::move(poller)},
      endpoint_{endpoint},
      connection_info_{} {
  AE_TELE_DEBUG(TcpTransport);
}

LwipTcpTransport::~LwipTcpTransport() { Disconnect(); }

void LwipTcpTransport::Connect() {
  connection_info_.connection_state = ConnectionState::kConnecting;

  connection_action_.emplace(action_context_, *this);

  connection_action_subscriptions_.Push(
      connection_action_->SubscribeOnResult(
          [this](auto const &action) { OnConnected(action.get_socket()); }),
      connection_action_->SubscribeOnError(
          [this](auto const & /* action */) { OnConnectionFailed(); }),
      connection_action_->FinishedEvent().Subscribe(
          [this]() { connection_action_.reset(); }));
}

ConnectionInfo const &LwipTcpTransport::GetConnectionInfo() const {
  return connection_info_;
}

ITransport::ConnectionSuccessEvent::Subscriber
LwipTcpTransport::ConnectionSuccess() {
  return connection_success_event_;
}

ITransport::ConnectionErrorEvent::Subscriber
LwipTcpTransport::ConnectionError() {
  return connection_error_event_;
}

ITransport::DataReceiveEvent::Subscriber LwipTcpTransport::ReceiveEvent() {
  return data_receive_event_;
}

ActionView<PacketSendAction> LwipTcpTransport::Send(DataBuffer data,
                                                    TimePoint current_time) {
  AE_TELE_DEBUG(TcpTransportSend, "Send data size {} at {:%Y-%m-%d %H:%M:%S}",
                data.size(), current_time);
  assert(socket_ != kInvalidSocket);

  auto packet_data = std::vector<std::uint8_t>{};
  VectorWriter<PacketSize> vw{packet_data};
  auto os = omstream{vw};
  // copy data with size
  os << data;

  return socket_packet_queue_manager_.AddPacket(LwipTcpPacketSendAction{
      action_context_, socket_, std::move(packet_data), current_time});
}

void LwipTcpTransport::OnConnected(int socket) {
  socket_ = socket;

  socket_event_action_ = SocketEventAction{action_context_};

  socket_event_subscription_ = socket_event_action_.SubscribeOnResult(
      [this](auto const &) { OnSocketUpdate(ae::Now()); });

  socket_poll_subscription_ =
      poller_->Add(socket_).Subscribe([this](auto event) {
        if (event.descriptor != socket_) {
          return;
        }
        // notify about new event on socket
        socket_event_action_.Notify();
      });

  connection_info_.max_packet_size =
      LWIP_NETIF_MTU - 2;  // 2 bytes for packet size
  connection_info_.connection_state = ConnectionState::kConnected;
  connection_success_event_.Emit();
}

void LwipTcpTransport::OnConnectionFailed() {
  connection_info_.connection_state = ConnectionState::kDisconnected;
  connection_error_event_.Emit();
}

void LwipTcpTransport::OnSocketUpdate(TimePoint current_time) {
  if (socket_ == kInvalidSocket) {
    return;
  }

  ReadSocket(current_time);

  socket_packet_queue_manager_.Send();
}

void LwipTcpTransport::ReadSocket(TimePoint current_time) {
  DataBuffer data(LWIP_NETIF_MTU);

  auto r = recv(socket_, data.data(), data.size(), 0);

  if (r < 0) {
    if ((errno != EAGAIN) && (errno != EWOULDBLOCK)) {
      AE_TELED_ERROR("Recv error: {} {}", errno, strerror(errno));
      Disconnect();
    }
  } else if (r == 0) {  // Connection closed
    AE_TELED_ERROR("Connection closed");
    Disconnect();
  } else {  // Data received
    AE_TELE_DEBUG(TcpTransportOnData, "Get data size {}", data.size());
    data.resize(static_cast<std::size_t>(r));
    data_packet_collector_.AddData(std::move(data));
    OnDataReceived(current_time);
  }
}

void LwipTcpTransport::OnDataReceived(TimePoint current_time) {
  for (auto data = data_packet_collector_.PopPacket(); !data.empty();
       data = data_packet_collector_.PopPacket()) {
    AE_TELE_DEBUG(TcpTransportReceive, "Receive data size {}", data.size());
    data_receive_event_.Emit(std::move(data), current_time);
  }
}

void LwipTcpTransport::Disconnect() {
  AE_TELE_DEBUG(TcpTransportDisconnect, "Disconnect from {}", endpoint_);
  connection_info_.connection_state = ConnectionState::kDisconnected;
  if (socket_ == kInvalidSocket) {
    return;
  }

  socket_event_subscription_.Reset();

  poller_->Remove(socket_);

  if (close(socket_) != 0) {
    return;
  }
  socket_ = kInvalidSocket;

  OnConnectionFailed();
}

}  // namespace ae
#endif  // defined LWIP_TCP_TRANSPORT_ENABLED
