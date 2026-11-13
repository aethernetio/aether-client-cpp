/*
 * Copyright 2026 Aethernet Inc.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy at http://www.apache.org/licenses/LICENSE-2.0
 */
#include "aether/transport/emulated/emulated_datagram_socket.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <utility>

namespace ae {
namespace emulated_datagram_socket_internal {
struct SocketState {
  bool connected = false;
  bool waiting_for_write = false;
  std::uint64_t generation = 0;
  ISocket::ReadyToWriteCb ready;
  ISocket::RecvDataCb recv;
  ISocket::ErrorCb error;
};
}  // namespace emulated_datagram_socket_internal

EmulatedDatagramNetwork::EmulatedDatagramNetwork(std::uint64_t seed) {
  // Xorshift requires nonzero state. Distinct seeds keep the two directions
  // reproducible even when feedback traffic changes independently.
  directions_[0].random_state = seed == 0 ? 1 : seed;
  directions_[1].random_state = seed ^ 0x9e3779b97f4a7c15ULL;
  if (directions_[1].random_state == 0) {
    directions_[1].random_state = 1;
  }
}

bool EmulatedDatagramNetwork::Later::operator()(Packet const& left,
                                                Packet const& right) const {
  return left.due != right.due ? left.due > right.due
                               : left.order > right.order;
}

void EmulatedDatagramNetwork::SetProfile(std::size_t direction,
                                         EmulatedDatagramProfile profile) {
  assert(direction < directions_.size() && "Invalid emulated direction");
  profile.loss_per_mille = std::min(profile.loss_per_mille, 1000U);
  profile.duplicate_per_mille = std::min(profile.duplicate_per_mille, 1000U);
  profile.reorder_per_mille = std::min(profile.reorder_per_mille, 1000U);
  directions_[direction].profile = profile;
}

EmulatedDatagramStats const& EmulatedDatagramNetwork::stats(
    std::size_t direction) const {
  assert(direction < directions_.size() && "Invalid emulated direction");
  return directions_[direction].stats;
}

std::shared_ptr<EmulatedDatagramNetwork::SocketState>
EmulatedDatagramNetwork::Register(std::size_t endpoint) {
  assert(endpoint < endpoints_.size() && "Invalid emulated endpoint");
  assert(endpoints_[endpoint].expired() && "Endpoint already has a socket");
  auto state = std::make_shared<SocketState>();
  endpoints_[endpoint] = state;
  return state;
}

void EmulatedDatagramNetwork::Unregister(
    std::size_t endpoint, std::shared_ptr<SocketState> const& state) {
  if (endpoints_[endpoint].lock() == state) {
    endpoints_[endpoint].reset();
  }
}

std::uint64_t EmulatedDatagramNetwork::Random(Direction& direction) {
  auto value = direction.random_state;
  value ^= value >> 12;
  value ^= value << 25;
  value ^= value >> 27;
  direction.random_state = value;
  return value * 0x2545f4914f6cdd1dULL;
}

bool EmulatedDatagramNetwork::Sample(Direction& direction,
                                     std::uint32_t per_mille) {
  return per_mille != 0 && Random(direction) % 1000 < per_mille;
}

void EmulatedDatagramNetwork::DrainReservations(Direction& direction) {
  while (!direction.reservations.empty() &&
         direction.reservations.front().departure <= now_) {
    direction.queued_bytes -= direction.reservations.front().bytes;
    direction.reservations.pop_front();
  }
}

void EmulatedDatagramNetwork::Send(std::size_t direction,
                                   Span<std::uint8_t> data) {
  auto& link = directions_[direction];
  ++link.stats.sent;
  link.stats.bytes_sent += data.size();
  if (!Enqueue(direction, data, true)) {
    return;
  }
  if (Sample(link, link.profile.duplicate_per_mille) &&
      Enqueue(direction, data, false)) {
    ++link.stats.duplicates;
  }
}

bool EmulatedDatagramNetwork::Enqueue(std::size_t direction,
                                      Span<std::uint8_t> data, bool primary) {
  auto& link = directions_[direction];
  auto const& profile = link.profile;
  auto& stats = link.stats;
  DrainReservations(link);
  auto const limit = std::min(profile.queue_bytes, kMaxPendingBytes);
  if (profile.bytes_per_second == 0 || data.size() > limit ||
      link.queued_bytes > limit - data.size() ||
      link.reservations.size() >= kMaxPendingPackets ||
      link.pending_packets >= kMaxPendingPackets ||
      data.size() > kMaxPendingBytes - link.pending_bytes) {
    ++stats.dropped_queue;
    return false;
  }

  // Inputs are capped by the memory bound, so the multiplication cannot
  // overflow.
  auto const byte_microseconds =
      static_cast<std::uint64_t>(data.size()) * 1000000ULL;
  auto const micros =
      byte_microseconds / profile.bytes_per_second +
      (byte_microseconds % profile.bytes_per_second != 0 ? 1 : 0);
  auto const departure =
      std::max(now_, link.available_at) + std::chrono::microseconds{micros};
  link.available_at = departure;
  link.reservations.push_back({departure, data.size()});
  link.queued_bytes += data.size();
  stats.peak_queue_bytes = std::max(stats.peak_queue_bytes, link.queued_bytes);

  // Network losses consume serialization bandwidth, just like UDP on a wire.
  if (profile.blackhole) {
    ++stats.dropped_blackhole;
    return true;
  }
  if (primary && profile.burst_every != 0 &&
      (stats.sent - 1) % profile.burst_every <
          std::min(profile.burst_length, profile.burst_every)) {
    ++stats.dropped_burst;
    return true;
  }
  if (Sample(link, profile.loss_per_mille)) {
    ++stats.dropped_random;
    return true;
  }

  std::int64_t delay_us = profile.delay.count();
  if (profile.jitter.count() != 0) {
    auto const jitter = static_cast<std::uint64_t>(profile.jitter.count());
    delay_us += static_cast<std::int64_t>(Random(link) % (2 * jitter + 1)) -
                static_cast<std::int64_t>(jitter);
  }
  if (Sample(link, profile.reorder_per_mille)) {
    delay_us += profile.reorder_delay.count();
    ++stats.reordered;
  }
  auto peer = endpoints_[1 - direction].lock();
  auto due = departure +
             std::chrono::microseconds{std::max<std::int64_t>(delay_us, 0)};
  packets_.push_back(Packet{due, next_order_++, direction, peer,
                            peer != nullptr ? peer->generation : 0,
                            DataBuffer{data.begin(), data.end()}});
  std::push_heap(packets_.begin(), packets_.end(), Later{});
  link.pending_bytes += data.size();
  ++link.pending_packets;
  return true;
}

void EmulatedDatagramNetwork::NotifyWriters() {
  for (std::size_t endpoint = 0; endpoint < endpoints_.size(); ++endpoint) {
    auto state = endpoints_[endpoint].lock();
    if (state != nullptr && state->connected && state->waiting_for_write &&
        !directions_[endpoint].profile.blocked) {
      state->waiting_for_write = false;
      if (state->ready) {
        state->ready();
      }
    }
  }
}

void EmulatedDatagramNetwork::AdvanceTo(TimePoint time) {
  assert(time >= now_ && "Emulated time must be monotonic");
  assert(!advancing_ && "AdvanceTo cannot be called from a network callback");
  advancing_ = true;
  NotifyWriters();
  while (!packets_.empty() && packets_.front().due <= time) {
    std::pop_heap(packets_.begin(), packets_.end(), Later{});
    auto packet = std::move(packets_.back());
    packets_.pop_back();
    now_ = packet.due;
    auto& link = directions_[packet.direction];
    link.pending_bytes -= packet.data.size();
    --link.pending_packets;
    DrainReservations(link);
    auto destination = packet.destination.lock();
    if (destination != nullptr && destination->connected &&
        destination->generation == packet.generation && destination->recv) {
      ++link.stats.delivered;
      // The shared state keeps this callback alive if it destroys its socket.
      destination->recv(Span{packet.data.data(), packet.data.size()});
    } else {
      ++link.stats.dropped_disconnected;
    }
    NotifyWriters();
  }
  now_ = time;
  for (auto& link : directions_) {
    DrainReservations(link);
  }
  NotifyWriters();
  advancing_ = false;
}

EmulatedDatagramSocket::EmulatedDatagramSocket(EmulatedDatagramNetwork& network,
                                               std::size_t endpoint)
    : network_{&network},
      endpoint_{endpoint},
      state_{network.Register(endpoint)} {}

EmulatedDatagramSocket::~EmulatedDatagramSocket() { Release(); }

EmulatedDatagramSocket::EmulatedDatagramSocket(
    EmulatedDatagramSocket&& other) noexcept
    : network_{std::exchange(other.network_, nullptr)},
      endpoint_{other.endpoint_},
      state_{std::move(other.state_)} {}

EmulatedDatagramSocket& EmulatedDatagramSocket::operator=(
    EmulatedDatagramSocket&& other) noexcept {
  if (this != &other) {
    Release();
    network_ = std::exchange(other.network_, nullptr);
    endpoint_ = other.endpoint_;
    state_ = std::move(other.state_);
  }
  return *this;
}

ISocket& EmulatedDatagramSocket::ReadyToWrite(ReadyToWriteCb callback) {
  state_->ready = std::move(callback);
  return *this;
}

ISocket& EmulatedDatagramSocket::RecvData(RecvDataCb callback) {
  state_->recv = std::move(callback);
  return *this;
}

ISocket& EmulatedDatagramSocket::Error(ErrorCb callback) {
  state_->error = std::move(callback);
  return *this;
}

ISocket& EmulatedDatagramSocket::Connect(AddressPort const&,
                                         ConnectedCb callback) {
  state_->connected = true;
  ++state_->generation;
  if (callback) {
    callback(ConnectionState::kConnected);
  }
  return *this;
}

std::optional<std::size_t> EmulatedDatagramSocket::Send(
    Span<std::uint8_t> data) {
  if (network_ == nullptr || !state_->connected ||
      data.size() > network_->directions_[endpoint_].profile.mtu) {
    return std::nullopt;
  }
  if (network_->directions_[endpoint_].profile.blocked) {
    state_->waiting_for_write = true;
    return 0;
  }
  network_->Send(endpoint_, data);
  return data.size();
}

void EmulatedDatagramSocket::Disconnect() {
  if (state_ != nullptr) {
    state_->connected = false;
    state_->waiting_for_write = false;
    ++state_->generation;
  }
}

void EmulatedDatagramSocket::Release() {
  if (network_ != nullptr) {
    Disconnect();
    network_->Unregister(endpoint_, state_);
    network_ = nullptr;
    state_.reset();
  }
}
}  // namespace ae
