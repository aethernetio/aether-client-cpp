/*
 * Copyright 2026 Aethernet Inc.
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

#ifndef AETHER_EVENTS_DETAILS_EVENT_KEY_REGISTRY_H_
#define AETHER_EVENTS_DETAILS_EVENT_KEY_REGISTRY_H_

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <etl/flat_map.h>

namespace ae::events {
// Event identifiers have one fixed wire-independent representation throughout
// the event system.
using Key = std::uint32_t;

/**
 * \brief Events key registry
 */
template <std::size_t Capacity>
class KeyRegistry {
 public:
  using Key = ::ae::events::Key;
  std::optional<Key> Register();
  void Retire(Key key);
  void Release(Key key);
  bool IsRegistered(Key key) const;
  bool IsRetiring(Key key) const;

 private:
  enum class State : std::uint8_t { kRegistered, kRetiring };

  Key next_key_{};
  etl::flat_map<Key, State, Capacity> key_map_;
};

template <std::size_t C>
auto KeyRegistry<C>::Register() -> std::optional<Key> {
  if (key_map_.full()) {
    return std::nullopt;
  }
  // Try every value in the unsigned key range, including Key::max().
  // Unsigned increment is defined to wrap to zero.
  auto const first_key = next_key_;
  while (true) {
    auto const k = next_key_;
    ++next_key_;
    auto [_, inserted] = key_map_.emplace(k, State::kRegistered);
    if (inserted) {
      return k;
    }
    if (next_key_ == first_key) {
      break;
    }
  }

  return std::nullopt;
}

template <std::size_t C>
void KeyRegistry<C>::Retire(Key key) {
  auto const i = key_map_.find(key);
  if (i != key_map_.end() && i->second == State::kRegistered) {
    i->second = State::kRetiring;
  }
}

template <std::size_t C>
void KeyRegistry<C>::Release(Key key) {
  auto const i = key_map_.find(key);
  assert(i != key_map_.end() && i->second == State::kRetiring &&
         "Only retiring keys may be released");
  if (i != key_map_.end() && i->second == State::kRetiring) {
    key_map_.erase(i);
  }
}

template <std::size_t C>
bool KeyRegistry<C>::IsRegistered(Key key) const {
  auto const i = key_map_.find(key);
  return i != key_map_.end() && i->second == State::kRegistered;
}

template <std::size_t C>
bool KeyRegistry<C>::IsRetiring(Key key) const {
  auto const i = key_map_.find(key);
  return i != key_map_.end() && i->second == State::kRetiring;
}

}  // namespace ae::events

#endif  // AETHER_EVENTS_DETAILS_EVENT_KEY_REGISTRY_H_
