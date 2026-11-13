/*
 * Copyright 2026 Aethernet Inc.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy at http://www.apache.org/licenses/LICENSE-2.0
 */
#ifndef AETHER_TRANSPORT_EMULATED_EMULATED_DATAGRAM_SOCKET_H_
#define AETHER_TRANSPORT_EMULATED_EMULATED_DATAGRAM_SOCKET_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

#include "aether/clock.h"
#include "aether/transport/system_sockets/sockets/isocket.h"
#include "aether/types/data_buffer.h"

namespace ae {
namespace emulated_datagram_socket_internal {
struct SocketState;
}

struct EmulatedDatagramProfile {
  Duration delay{10ms};
  Duration jitter{0us};
  std::uint32_t loss_per_mille = 0;
  std::uint32_t duplicate_per_mille = 0;
  std::uint32_t reorder_per_mille = 0;
  Duration reorder_delay{20ms};
  std::uint64_t bytes_per_second = 1000000;
  std::size_t queue_bytes = 64000;
  std::size_t mtu = 1200;
  bool blackhole = false;
  bool blocked = false;
  // Drop the first burst_length primary datagrams of every burst_every sends.
  // A zero period disables burst loss; length is capped at the period.
  std::size_t burst_every = 0;
  std::size_t burst_length = 0;
};

struct EmulatedDatagramStats {
  // Primary datagrams accepted by Send(), including subsequent network drops.
  std::uint64_t sent = 0;
  std::uint64_t delivered = 0;
  std::uint64_t dropped_random = 0;
  std::uint64_t dropped_queue = 0;
  std::uint64_t dropped_burst = 0;
  std::uint64_t dropped_blackhole = 0;
  std::uint64_t dropped_disconnected = 0;
  std::uint64_t duplicates = 0;
  std::uint64_t reordered = 0;
  std::uint64_t bytes_sent = 0;
  std::size_t peak_queue_bytes = 0;
};

class EmulatedDatagramSocket;

// Single-threaded virtual network. It must outlive both sockets. Direction i is
// endpoint i -> endpoint (1-i); profiles and PRNG state are independent.
class EmulatedDatagramNetwork {
 public:
  explicit EmulatedDatagramNetwork(std::uint64_t seed = 1);
  EmulatedDatagramNetwork(EmulatedDatagramNetwork const&) = delete;
  EmulatedDatagramNetwork& operator=(EmulatedDatagramNetwork const&) = delete;

  TimePoint now() const { return now_; }
  void AdvanceTo(TimePoint time);
  // Changes affect future submissions; already serialized packets keep their
  // scheduled arrival. Unblocking notifies the writer on the next AdvanceTo.
  void SetProfile(std::size_t direction, EmulatedDatagramProfile profile);
  EmulatedDatagramStats const& stats(std::size_t direction) const;

 private:
  friend class EmulatedDatagramSocket;
  using SocketState = emulated_datagram_socket_internal::SocketState;
  struct Reservation {
    TimePoint departure;
    std::size_t bytes;
  };
  struct Direction {
    EmulatedDatagramProfile profile;
    EmulatedDatagramStats stats;
    std::uint64_t random_state = 1;
    TimePoint available_at{};
    std::deque<Reservation> reservations;
    std::size_t queued_bytes = 0;
    std::size_t pending_bytes = 0;
    std::size_t pending_packets = 0;
  };
  struct Packet {
    TimePoint due;
    std::uint64_t order;
    std::size_t direction;
    std::weak_ptr<SocketState> destination;
    std::uint64_t generation;
    DataBuffer data;
  };
  struct Later {
    bool operator()(Packet const& left, Packet const& right) const;
  };

  std::shared_ptr<SocketState> Register(std::size_t endpoint);
  void Unregister(std::size_t endpoint,
                  std::shared_ptr<SocketState> const& state);
  void Send(std::size_t direction, Span<std::uint8_t> data);
  bool Enqueue(std::size_t direction, Span<std::uint8_t> data, bool primary);
  void DrainReservations(Direction& direction);
  void NotifyWriters();
  static std::uint64_t Random(Direction& direction);
  static bool Sample(Direction& direction, std::uint32_t per_mille);

  // Independent of link queue size: bound memory for long propagation delays.
  static constexpr std::size_t kMaxPendingPackets = 65536;
  static constexpr std::size_t kMaxPendingBytes = 64 * 1024 * 1024;
  TimePoint now_{};
  bool advancing_ = false;
  std::uint64_t next_order_ = 0;
  std::array<Direction, 2> directions_;
  std::array<std::weak_ptr<SocketState>, 2> endpoints_;
  std::vector<Packet> packets_;
};

class EmulatedDatagramSocket final : public ISocket {
 public:
  EmulatedDatagramSocket(EmulatedDatagramNetwork& network,
                         std::size_t endpoint);
  ~EmulatedDatagramSocket() override;
  EmulatedDatagramSocket(EmulatedDatagramSocket const&) = delete;
  EmulatedDatagramSocket& operator=(EmulatedDatagramSocket const&) = delete;
  EmulatedDatagramSocket(EmulatedDatagramSocket&& other) noexcept;
  EmulatedDatagramSocket& operator=(EmulatedDatagramSocket&& other) noexcept;

  ISocket& ReadyToWrite(ReadyToWriteCb callback) override;
  ISocket& RecvData(RecvDataCb callback) override;
  ISocket& Error(ErrorCb callback) override;
  ISocket& Connect(AddressPort const& destination,
                   ConnectedCb callback) override;
  std::optional<std::size_t> Send(Span<std::uint8_t> data) override;
  void Disconnect() override;

 private:
  void Release();
  EmulatedDatagramNetwork* network_;
  std::size_t endpoint_;
  std::shared_ptr<emulated_datagram_socket_internal::SocketState> state_;
};
}  // namespace ae

#endif  // AETHER_TRANSPORT_EMULATED_EMULATED_DATAGRAM_SOCKET_H_
