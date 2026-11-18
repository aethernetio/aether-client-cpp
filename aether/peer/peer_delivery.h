// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_PEER_PEER_DELIVERY_H_
#define AETHER_PEER_PEER_DELIVERY_H_
#include <algorithm>
#include <array>
#include <map>
#include <utility>
#include "aether/peer/peer_types.h"
namespace ae {
Uid NewPeerDeliveryEpoch();
// Shared framing for opaque cloud payloads and authenticated direct payloads.
struct PeerDelivery {
  AE_REFLECT_MEMBERS(magic, epoch, number, stream, data, service)
  std::uint32_t magic{0x31444d41};
  Uid epoch{};
  std::uint64_t number{};
  std::uint32_t stream{};
  DataBuffer data;
  PeerService service{};
};

class PeerDeliveryWindow {
 public:
  bool Accept(Uid const& sender, PeerDelivery const& message) {
    if (message.magic != 0x31444d41 || message.epoch.empty() ||
        message.number == 0) {
      return false;
    }
    auto key = std::pair{sender, message.epoch};
    auto found = windows_.find(key);
    if (found == windows_.end()) {
      if (windows_.size() >= 256) {
        auto oldest = std::min_element(windows_.begin(), windows_.end(),
                                       [](auto const& a, auto const& b) {
                                         return a.second.used < b.second.used;
                                       });
        windows_.erase(oldest);
      }
      found = windows_.emplace(key, Window{}).first;
    }
    auto& window = found->second;
    window.used = ++clock_;
    if (message.number > window.high) {
      auto const gap = message.number - window.high;
      if (gap >= 256) {
        window.bits = {};
      } else {
        for (std::size_t bit = 256; bit-- > 0;) {
          bool value = bit >= gap && Test(window, bit - gap);
          Set(window, bit, value);
        }
      }
      window.high = message.number;
      Set(window, 0, true);
      return true;
    }
    auto const age = window.high - message.number;
    if (age >= 256 || Test(window, static_cast<std::size_t>(age))) {
      return false;
    }
    Set(window, static_cast<std::size_t>(age), true);
    return true;
  }

 private:
  struct Window {
    std::uint64_t high{};
    std::uint64_t used{};
    std::array<std::uint64_t, 4> bits{};
  };
  static bool Test(Window const& window, std::size_t bit) {
    return (window.bits[bit / 64] & (std::uint64_t{1} << (bit % 64))) != 0;
  }
  static void Set(Window& window, std::size_t bit, bool value) {
    auto mask = std::uint64_t{1} << (bit % 64);
    if (value) {
      window.bits[bit / 64] |= mask;
    } else {
      window.bits[bit / 64] &= ~mask;
    }
  }
  std::map<std::pair<Uid, Uid>, Window> windows_;
  std::uint64_t clock_{};
};
}  // namespace ae
#endif
