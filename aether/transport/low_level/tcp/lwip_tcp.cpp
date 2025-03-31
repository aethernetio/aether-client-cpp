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
      transport_{&transport},
      state_{State::kConnecting},
      state_changed_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {
  Connect();
}

TimePoint LwipTcpTransport::ConnectionAction::Update(TimePoint current_time) {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kWaitConnection:
        break;
      case State::kConnectionUpdate:
        ConnectUpdate();
        break;
      case State::kConnected:
        Action::Result(*this);
        break;
      case State::kConnectionFailed:
        Action::Error(*this);
        break;
      default:
        break;
    }
  }
  return current_time;
}

void LwipTcpTransport::ConnectionAction::Connect() {
  struct sockaddr_in servaddr;
  timeval tv;
  socklen_t optlen;
  int on = 1;
  int res = 0;

  AE_TELE_DEBUG(TcpTransportConnect, "Connect to {}", transport_->endpoint_);

  if ((socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_IP)) < 0) {
    AE_TELED_ERROR("Socket not created");
    state_ = State::kConnectionFailed;
    return;
  }

  // make socket nonblocking
  if (lwip_fcntl(socket_, F_SETFL, O_NONBLOCK) != ESP_OK) {
    AE_TELED_ERROR("lwip_fcntl set nonblocking mode error {} {}", errno,
                   strerror(errno));
    close(socket_);
    socket_ = kInvalidSocket;
    state_ = State::kConnectionFailed;
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
    state_ = State::kConnectionFailed;
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
    state_ = State::kConnectionFailed;
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
    state_ = State::kConnectionFailed;
    return;
  }

  AE_TELED_DEBUG("Socket created");
  memset(&servaddr, 0, sizeof(servaddr));
  // Fill server information
  auto addr_string = ae::Format("{}", transport_->endpoint_.ip);

  servaddr.sin_family = AF_INET;  // IPv4
  servaddr.sin_addr.s_addr = inet_addr(addr_string.c_str());
  servaddr.sin_port = htons(transport_->endpoint_.port);

  if (connect(socket_, (struct sockaddr *)&servaddr,
              sizeof(struct sockaddr_in)) < 0) {
    if ((errno == EAGAIN) || (errno == EINPROGRESS)) {
      AE_TELED_DEBUG("Wait connection");
      WaitConnection();
    } else {
      AE_TELED_ERROR("Not connected {} {}", errno, strerror(errno));
      close(socket_);
      socket_ = kInvalidSocket;
      state_ = State::kConnectionFailed;
    }
    return;
  }
  AE_TELED_DEBUG("Connected to {}", transport_->endpoint_);
  transport_->OnConnected(socket_);
  state_ = State::kConnected;
}

void LwipTcpTransport::ConnectionAction::WaitConnection() {
  state_ = State::kWaitConnection;

  auto poller_ptr = transport_->poller_;
  if (!poller_ptr) {
    AE_TELED_ERROR("Poller is null");
    state_ = State::kConnectionFailed;
    return;
  }

  poller_subscription_ =
      poller_ptr->Add(socket_).Subscribe([this](auto const &event) {
        if (event.descriptor != socket_) {
          return;
        }
        // remove from poller
        auto poller_ptr = transport_->poller_;
        assert(poller_ptr);
        poller_ptr->Remove(socket_);
        // check connection state
        state_ = State::kConnectionUpdate;
        poller_subscription_.Reset();
      });
}

void LwipTcpTransport::ConnectionAction::ConnectUpdate() {
  // check socket status
  int err;
  socklen_t len = sizeof(len);
  if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, static_cast<void *>(&err),
                 &len) != 0) {
    AE_TELED_ERROR("Getsockopt error {}, {}", errno, strerror(errno));
    socket_ = kInvalidSocket;
    close(socket_);
    state_ = State::kConnectionFailed;
    return;
  }
  if (err != 0) {
    AE_TELED_ERROR("Connect error {}, {}", err, strerror(err));
    socket_ = kInvalidSocket;
    close(socket_);
    state_ = State::kConnectionFailed;
    return;
  }

  AE_TELED_DEBUG("Waited for connection to {}", transport_->endpoint_);
  transport_->OnConnected(socket_);
  state_ = State::kConnected;
}

LwipTcpTransport::LwipTcpPacketSendAction::LwipTcpPacketSendAction(
    ActionContext action_context, LwipTcpTransport &transport, DataBuffer data,
    TimePoint current_time)
    : SocketPacketSendAction{action_context},
      transport_{&transport},
      data_{data},
      current_time_{current_time},
      sent_offset_{0},
      state_changed_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {}

LwipTcpTransport::LwipTcpPacketSendAction::LwipTcpPacketSendAction(
    LwipTcpPacketSendAction &&other) noexcept
    : SocketPacketSendAction{std::move(other)},
      transport_{other.transport_},
      data_{std::move(other.data_)},
      current_time_{other.current_time_},
      sent_offset_{other.sent_offset_},
      state_changed_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })} {}

void LwipTcpTransport::LwipTcpPacketSendAction::Send() {
  state_.Set(State::kProgress);

  if (!transport_->socket_lock_.try_lock()) {
    Action::Trigger();
    return;
  }
  auto lock = std::lock_guard{transport_->socket_lock_, std::adopt_lock};

  auto size_to_send = data_.size() - sent_offset_;
  auto r =
      send(transport_->socket_, data_.data() + sent_offset_, size_to_send, 0);

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

LwipTcpTransport::LwipTcpReadAction::LwipTcpReadAction(
    ActionContext action_context, LwipTcpTransport &transport)
    : Action{action_context},
      transport_{&transport},
      read_buffer_(LWIP_NETIF_MTU) {}

TimePoint LwipTcpTransport::LwipTcpReadAction::Update(TimePoint current_time) {
  if (read_event_.exchange(false)) {
    DataReceived(current_time);
  }
  if (error_.exchange(false)) {
    Action::Error(*this);
  }

  return current_time;
}

void LwipTcpTransport::LwipTcpReadAction::Read() {
  auto lock = std::lock_guard{transport_->socket_lock_};
  while (true) {
    auto res =
        recv(transport_->socket_, read_buffer_.data(), read_buffer_.size(), 0);
    if (res < 0) {
      // No data
      if ((errno == EWOULDBLOCK) || (errno == EAGAIN)) {
        break;
      }
      AE_TELED_ERROR("Recv error {} {}", errno, strerror(errno));
      error_ = true;
      break;
    }
    data_packet_collector_.AddData(read_buffer_.data(),
                                   static_cast<std::size_t>(res));
    read_event_ = true;
  }
  if (error_ || read_event_) {
    Action::Trigger();
  }
}

void LwipTcpTransport::LwipTcpReadAction::DataReceived(TimePoint current_time) {
  auto lock = std::lock_guard{transport_->socket_lock_};
  for (auto data = data_packet_collector_.PopPacket(); !data.empty();
       data = data_packet_collector_.PopPacket()) {
    AE_TELE_DEBUG(TcpTransportReceive, "Receive data size {}", data.size());
    transport_->data_receive_event_.Emit(data, current_time);
  }
}

LwipTcpTransport::LwipTcpTransport(ActionContext action_context,
                                   IPoller::ptr poller,
                                   IpAddressPort const &endpoint)
    : action_context_{action_context},
      poller_{std::move(poller)},
      endpoint_{endpoint},
      connection_info_{},
      socket_packet_queue_manager_{action_context_} {
  AE_TELE_DEBUG(TcpTransport);
}

LwipTcpTransport::~LwipTcpTransport() { Disconnect(); }

void LwipTcpTransport::Connect() {
  connection_info_.connection_state = ConnectionState::kConnecting;

  connection_action_.emplace(action_context_, *this);

  connection_action_subs_.Push(
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

  auto send_action =
      socket_packet_queue_manager_.AddPacket(LwipTcpPacketSendAction{
          action_context_, *this, std::move(packet_data), current_time});
  send_action_subs_.Push(send_action->SubscribeOnError([this](auto const &) {
    AE_TELED_ERROR("Send error, disconnect!");
    Disconnect();
  }));
  return send_action;
}

void LwipTcpTransport::OnConnected(int socket) {
  socket_ = socket;

  read_action_.emplace(action_context_, *this);
  read_action_subs_.Push(
      read_action_->SubscribeOnError([this](auto const & /* action */) {
        AE_TELED_ERROR("Read error, disconnect!");
        Disconnect();
      }),
      read_action_->FinishedEvent().Subscribe(
          [this]() { read_action_.reset(); }));

  socket_error_action_ = SocketEventAction{action_context_};
  socket_error_subscription_ = socket_error_action_.SubscribeOnResult(
      [this](auto const &) { Disconnect(); });

  socket_poll_subscription_ =
      poller_->Add(socket_).Subscribe([this](auto event) {
        if (event.descriptor != socket_) {
          return;
        }
        switch (event.event_type) {
          case EventType::kRead:
            read_action_->Read();
            break;
          case EventType::kWrite:
            socket_packet_queue_manager_.Send();
            break;
          default:  // EventType::kError:
            poller_->Remove(socket_);
            socket_error_action_.Notify();
            break;
        }
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

void LwipTcpTransport::Disconnect() {
  auto lock = std::lock_guard(socket_lock_);
  AE_TELE_DEBUG(TcpTransportDisconnect, "Disconnect from {}", endpoint_);
  connection_info_.connection_state = ConnectionState::kDisconnected;
  if (socket_ == kInvalidSocket) {
    return;
  }

  socket_error_subscription_.Reset();

  poller_->Remove(socket_);

  if (close(socket_) != 0) {
    return;
  }
  socket_ = kInvalidSocket;

  OnConnectionFailed();
}

}  // namespace ae
#endif  // defined LWIP_TCP_TRANSPORT_ENABLED
