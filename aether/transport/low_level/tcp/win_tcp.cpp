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

#include "aether/transport/low_level/tcp/win_tcp.h"

#if defined WIN_TCP_TRANSPORT_ENABLED

#  include <winsock2.h>
#  include <ws2def.h>
#  include <ws2ipdef.h>

#  include <vector>
#  include <future>
#  include <cassert>
#  include <utility>
#  include <algorithm>

#  include "aether/env.h"
#  include "aether/transport/transport_tele.h"

namespace ae {
static constexpr auto kBufferMaxSize = 1500;  // bytes

namespace _internal {
using SockAddrPtr =
    std::unique_ptr<struct sockaddr, void (*)(struct sockaddr*)>;

struct SockAddr {
  SockAddrPtr ptr;
  std::size_t addr_len;
};

SockAddr MakeAddrInfo(IpAddressPort const& end_point) {
  if (end_point.ip.version == IpAddress::Version::kIpV4) {
    auto* addr = new sockaddr_in();
    addr->sin_family = AF_INET;
    addr->sin_port = SwapToInet(end_point.port);
    std::copy(std::begin(end_point.ip.value.ipv4_value),
              std::end(end_point.ip.value.ipv4_value),
              reinterpret_cast<std::uint8_t*>(&addr->sin_addr));
    return SockAddr{
        {reinterpret_cast<sockaddr*>(addr),
         [](auto* addr) { delete reinterpret_cast<sockaddr_in*>(addr); }},
        sizeof(sockaddr_in)};

  } else {
    auto* addr = new sockaddr_in6();
    addr->sin6_family = AF_INET6;
    addr->sin6_port = SwapToInet(end_point.port);
    std::copy(std::begin(end_point.ip.value.ipv6_value),
              std::end(end_point.ip.value.ipv6_value),
              reinterpret_cast<std::uint8_t*>(&addr->sin6_addr));

    return SockAddr{
        {reinterpret_cast<sockaddr*>(addr),
         [](auto* addr) { delete reinterpret_cast<sockaddr_in6*>(addr); }},
        sizeof(sockaddr_in6)};
  }
}
}  // namespace _internal

WinTcpTransport::ConnectionAction::ConnectionAction(
    ActionContext action_context, WinTcpTransport& transport)
    : Action{action_context},
      transport_{&transport},
      state_{},
      state_changed_subscription_{state_.changed_event().Subscribe(
          [this](auto) { Action::Trigger(); })},
      alive_checker_{std::make_shared<std::mutex>()} {
  Connect();
}

WinTcpTransport::ConnectionAction::~ConnectionAction() {
  // lock prevent destruction while this updated in Connect
  alive_checker_->lock();
}

TimePoint WinTcpTransport::ConnectionAction::Update(TimePoint current_time) {
  if (state_.changed()) {
    switch (state_.Acquire()) {
      case State::kWaitConnection:
        break;
      case State::kConnectionUpdate:
        ConnectionUpdate();
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

void WinTcpTransport::ConnectionAction::Connect() {
  AE_TELE_DEBUG(TcpTransportConnect, "Connect to {}", transport_->endpoint_);
  state_ = State::kWaitConnection;

  // run asynchronously
  auto _ = std::async([this, alive_observer{std::weak_ptr{alive_checker_}}] {
    // Work with local context on this different thread
    DescriptorType::Socket sock;
    State state;
    int on = 1;
    auto addr = _internal::MakeAddrInfo(transport_->endpoint_);
    // ::socket() sets WSA_FLAG_OVERLAPPED by default that allows us to use iocp
    sock = ::socket(addr.ptr->sa_family, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
      AE_TELED_ERROR("Got socket creation error {}", WSAGetLastError());
      state = State::kConnectionFailed;
      goto FINISH;
    }

    if (auto res = ::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
                                reinterpret_cast<char const*>(&on), sizeof(on));
        res == SOCKET_ERROR) {
      auto err_code = WSAGetLastError();
      AE_TELED_ERROR("Socket connect error {}", err_code);
      state = State::kConnectionFailed;
      ::closesocket(sock);
      sock = InvalidSocketValue;
      goto FINISH;
    }

    // FIXME: how to make non blocking connect with iocp poller
    // there is no defined overlapped operation for connect
    if (auto res =
            connect(sock, addr.ptr.get(), static_cast<int>(addr.addr_len));
        res == SOCKET_ERROR) {
      auto err_code = WSAGetLastError();
      AE_TELED_ERROR("Socket connect error {}", err_code);
      state = State::kConnectionFailed;
      ::closesocket(sock);
      sock = InvalidSocketValue;
      goto FINISH;
    }

    state = State::kConnectionUpdate;

  FINISH:
    // update this safely and only if it's alive
    // lock prevent destruction while this update
    if (auto alive_lock = alive_observer.lock();
        alive_lock && alive_lock->try_lock()) {
      auto lock = std::lock_guard{*alive_lock, std::adopt_lock};
      socket_ = sock;
      state_ = state;
    }
  });
}

void WinTcpTransport::ConnectionAction::ConnectionUpdate() {
  transport_->OnConnect(socket_);
  state_ = State::kConnected;
}

WinTcpTransport::WinTcpPacketSendAction::WinTcpPacketSendAction(
    ActionContext action_context, WinTcpTransport& transport, DataBuffer data,
    TimePoint current_time)
    : SocketPacketSendAction{action_context},
      transport_{&transport},
      send_buffer_{std::move(data)},
      current_time_{current_time},
      send_offset_{},
      state_changed_subscription_{
          state_.changed_event().Subscribe([this](auto state) {
            // unsubscribe from update event
            switch (state) {
              case State::kSuccess:
              case State::kFailed:
              case State::kPanic:
              case State::kTimeout:
              case State::kStopped:
                send_event_subscription_.Reset();
                break;
              default:
                break;
            }
          })},
      send_pending_{} {}

WinTcpTransport::WinTcpPacketSendAction::WinTcpPacketSendAction(
    WinTcpPacketSendAction&& other) noexcept
    : SocketPacketSendAction{std::move(other)},
      transport_{other.transport_},
      send_buffer_{std::move(other.send_buffer_)},
      current_time_{other.current_time_},
      send_offset_{other.send_offset_},
      state_changed_subscription_{
          state_.changed_event().Subscribe([this](auto state) {
            // unsubscribe from update event
            switch (state) {
              case State::kSuccess:
              case State::kFailed:
              case State::kPanic:
              case State::kTimeout:
              case State::kStopped:
                send_event_subscription_.Reset();
                break;
              default:
                break;
            }
          })},
      send_pending_{other.send_pending_.load()} {}

void WinTcpTransport::WinTcpPacketSendAction::Send() {
  if (state_.get() == State::kQueued) {
    send_event_subscription_ =
        EventSubscriber{transport_->send_event_}.Subscribe(
            *this, MethodPtr<&WinTcpPacketSendAction::OnSend>{});
    state_ = State::kProgress;
  } else if ((state_.get() == State::kProgress) &&
             (send_offset_ == send_buffer_.size())) {
    // all data sent
    state_ = State::kSuccess;
    return;
  }

  MakeSend();
}

void WinTcpTransport::WinTcpPacketSendAction::MakeSend() {
  if (send_pending_) {
    return;
  }

  if (!transport_->socket_lock_.try_lock()) {
    Action::Trigger();
    return;
  }
  auto lock = std::lock_guard{transport_->socket_lock_, std::adopt_lock};

  // Make a send request, result would be known in OnSend
  auto wsa_buffer =
      WSABUF{static_cast<ULONG>(send_buffer_.size() - send_offset_),
             reinterpret_cast<char*>(send_buffer_.data() + send_offset_)};
  DWORD bytes_transferred;
  auto res = WSASend(
      transport_->socket_, &wsa_buffer, 1, &bytes_transferred, 0,
      reinterpret_cast<OVERLAPPED*>(&transport_->write_overlapped_), nullptr);
  if (res == SOCKET_ERROR) {
    auto error_code = WSAGetLastError();
    if (error_code == WSA_IO_PENDING) {
      // need to wait new event
      // handle result in OnSend
      send_pending_ = true;
      return;
    } else {
      AE_TELED_ERROR("Socket send error {}", error_code);
      state_ = State::kFailed;
      return;
    }
  }
  send_offset_ += bytes_transferred;
  if (send_offset_ == send_buffer_.size()) {
    // all data sent
    state_ = State::kSuccess;
    return;
  }
}

void WinTcpTransport::WinTcpPacketSendAction::OnSend() {
  // this invoked from Poller thread
  auto lock = std::lock_guard{transport_->socket_lock_};
  DWORD bytes_transferred;
  DWORD flags;
  auto res = WSAGetOverlappedResult(
      transport_->socket_,
      reinterpret_cast<OVERLAPPED*>(&transport_->write_overlapped_),
      &bytes_transferred, false, &flags);

  if (!res) {
    AE_TELED_ERROR("WSAGetOverlappedResult get error {}", WSAGetLastError());
    return;
  }
  if (!send_pending_.exchange(false)) {
    // result already handled
    return;
  }

  // mark data as sent
  send_offset_ += bytes_transferred;
}

WinTcpTransport::WinTcpReadAction::WinTcpReadAction(
    ActionContext action_context, WinTcpTransport& transport)
    : Action{std::move(action_context)},
      transport_{&transport},
      recv_tmp_buffer_(kBufferMaxSize),
      read_event_{true},  // start with first RecvUpdate()
      error_event_{false} {}

TimePoint WinTcpTransport::WinTcpReadAction::Update(TimePoint current_time) {
  if (read_event_.exchange(false)) {
    RecvUpdate();
    HandleReceivedData();
  }
  if (error_event_.exchange(false)) {
    Action::Error(*this);
  }
  return current_time;
}

void WinTcpTransport::WinTcpReadAction::RecvUpdate() {
  auto lock = std::lock_guard{transport_->socket_lock_};

  auto wsabuf = WSABUF{static_cast<ULONG>(recv_tmp_buffer_.size()),
                       reinterpret_cast<char*>(recv_tmp_buffer_.data())};

  DWORD bytes_transferred;
  DWORD flags = 0;
  auto res = WSARecv(
      transport_->socket_, &wsabuf, 1, &bytes_transferred, &flags,
      reinterpret_cast<OVERLAPPED*>(&transport_->read_overlapped_), nullptr);
  if (res == SOCKET_ERROR) {
    auto err_code = WSAGetLastError();
    if (err_code != WSA_IO_PENDING) {
      AE_TELED_ERROR("Recv get error {}", err_code);
      error_event_ = true;
      Action::Trigger();
      return;
    }
  }
  // receive data made by OnRecv
}

void WinTcpTransport::WinTcpReadAction::HandleReceivedData() {
  auto lock = std::lock_guard{transport_->socket_lock_};

  for (auto packet = data_packet_collector_.PopPacket(); !packet.empty();
       packet = data_packet_collector_.PopPacket()) {
    // TODO: get time from action
    AE_TELE_DEBUG(TcpTransportReceive, "Receive data size {}", packet.size());
    transport_->data_receive_event_.Emit(std::move(packet),
                                         TimePoint::clock::now());
  }
}

void WinTcpTransport::WinTcpReadAction::OnRecv() {
  auto lock = std::lock_guard{transport_->socket_lock_};

  DWORD bytes_transferred;
  DWORD flags;
  auto res = WSAGetOverlappedResult(
      transport_->socket_,
      reinterpret_cast<OVERLAPPED*>(&transport_->read_overlapped_),
      &bytes_transferred, false, &flags);
  if (!res) {
    AE_TELED_ERROR("WSAGetOverlappedResult socket {} get error {}", *socket,
                   WSAGetLastError());
    error_event_ = true;
    Action::Trigger();
    return;
  }

  if (bytes_transferred == 0) {
    AE_TELED_ERROR("WSAGetOverlappedResult - got 0 bytes_transferred");
    error_event_ = true;
    Action::Trigger();
    return;
  }

  data_packet_collector_.AddData(recv_tmp_buffer_.data(),
                                 static_cast<std::size_t>(bytes_transferred));
  read_event_ = true;
  Action::Trigger();
}

WinTcpTransport::WinTcpTransport(ActionContext action_context,
                                 IPoller::ptr poller,
                                 IpAddressPort const& endpoint)
    : action_context_{action_context},
      poller_{std::move(poller)},
      endpoint_{endpoint},
      socket_{INVALID_SOCKET},
      socket_packet_queue_manager_{action_context_},
      read_overlapped_{{}, EventType::kRead},
      write_overlapped_{{}, EventType::kWrite} {
  AE_TELE_DEBUG(TcpTransport, "Created win tcp transport to endpoint {}",
                endpoint_);
  connection_info_.connection_state = ConnectionState::kUndefined;
}

WinTcpTransport::~WinTcpTransport() { Disconnect(); }

void WinTcpTransport::Connect() {
  connection_info_.connection_state = ConnectionState::kConnecting;

  connection_action_.emplace(action_context_, *this);

  connection_subs_.Push(
      connection_action_
          ->SubscribeOnError([this](auto const&) { OnConnectionError(); })
          .Once(),
      connection_action_->FinishedEvent().Subscribe(
          [this]() { connection_action_.reset(); }));
}

ConnectionInfo const& WinTcpTransport::GetConnectionInfo() const {
  return connection_info_;
}

ITransport::ConnectionSuccessEvent::Subscriber
WinTcpTransport::ConnectionSuccess() {
  return connection_success_event_;
}

ITransport::ConnectionErrorEvent::Subscriber
WinTcpTransport::ConnectionError() {
  return connection_error_event_;
}

ITransport::DataReceiveEvent::Subscriber WinTcpTransport::ReceiveEvent() {
  return data_receive_event_;
}

ActionView<PacketSendAction> WinTcpTransport::Send(DataBuffer data,
                                                   TimePoint current_time) {
  AE_TELE_DEBUG(TcpTransportSend,
                "Send data size {} at UTC :{:%Y-%m-%d %H:%M:%S}", data.size(),
                current_time);

  auto packet_data = std::vector<std::uint8_t>{};
  VectorWriter<PacketSize> vw{packet_data};
  auto os = omstream{vw};
  // copy data with size
  os << data;

  auto send_action =
      socket_packet_queue_manager_.AddPacket(WinTcpPacketSendAction{
          action_context_, *this, std::move(packet_data), current_time});
  send_action_subs_.Push(send_action->SubscribeOnError([this](auto const&) {
    AE_TELED_ERROR("Send error disconnect");
    Disconnect();
  }));

  return send_action;
}

void WinTcpTransport::OnConnect(DescriptorType::Socket socket) {
  socket_error_action_ = SocketEventAction{action_context_};
  socket_error_sub_ = socket_error_action_.SubscribeOnError(
      [this](auto const&) { OnSocketError(); });

  socket_ = socket;

  read_action_.emplace(action_context_, *this);
  read_error_sub_ = read_action_->SubscribeOnError([&](auto const&) {
    AE_TELED_ERROR("Read error");
    OnSocketError();
  });

  socket_poll_sub_ =
      poller_->Add(socket_).Subscribe([this, sock{socket_}](auto event) {
        if (event.descriptor != sock) {
          return;
        }
        switch (event.event_type) {
          case EventType::kRead:
            read_action_->OnRecv();
            break;
          case EventType::kWrite:
            send_event_.Emit();
            break;
          case EventType::kError:
          default:
            AE_TELED_ERROR("Got socket error event type {}", event.event_type);
            socket_error_action_.Notify();
            break;
        }
      });

  // -2 bytes for packet size
  connection_info_.max_packet_size = kBufferMaxSize - 2;
  connection_info_.connection_state = ConnectionState::kConnected;

  connection_success_event_.Emit();
}

void WinTcpTransport::OnConnectionError() {
  connection_info_.connection_state = ConnectionState::kDisconnected;
  connection_error_event_.Emit();
}

void WinTcpTransport::OnSocketError() { Disconnect(); }

void WinTcpTransport::Disconnect() {
  auto lock = std::lock_guard{socket_lock_};

  if (socket_ == INVALID_SOCKET) {
    return;
  }
  poller_->Remove(socket_);
  closesocket(socket_);
  socket_ = INVALID_SOCKET;

  socket_error_sub_.Reset();

  AE_TELE_DEBUG(TcpTransportDisconnect, "Disconnect from {}", endpoint_);
  OnConnectionError();
}
}  // namespace ae
#endif
