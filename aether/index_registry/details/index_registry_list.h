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

#ifndef AETHER_INDEX_REGISTRY_DETAILS_INDEX_REGISTRY_LIST_H_
#define AETHER_INDEX_REGISTRY_DETAILS_INDEX_REGISTRY_LIST_H_

#include <array>
#include <variant>
#include <cstddef>
#include <utility>
#include <cassert>

namespace ae::index_registry {
struct Reg {
  void* ptr;
};

class IndexRegistryListBase {
 protected:
  ~IndexRegistryListBase() = default;

 public:
  virtual constexpr std::size_t Put(Reg reg) noexcept = 0;
  virtual constexpr Reg* View(std::size_t index) noexcept = 0;
  virtual constexpr void Remove(std::size_t index) noexcept = 0;
  virtual constexpr std::size_t capacity() const noexcept = 0;
};

/**
 * \brief Pool of Regs available by index
 */
template <std::size_t Capacity>
class IndexRegistryList final : public IndexRegistryListBase {
  using Entry = std::variant<std::monostate, std::size_t, Reg>;

 public:
  constexpr IndexRegistryList() : next_{0}, entries_{} {
    entries_[0] = std::size_t{1};
  }

  constexpr std::size_t Put(Reg reg) noexcept override {
    // get the ref by next_ index and update next_ from the value stored in the
    // entry
    if (next_ == Capacity) {
      return Capacity;
    }
    auto index = next_;
    auto& e = entries_[next_];
    assert(std::holds_alternative<std::size_t>(e) && "Registry list is broken");
    next_ = std::get<std::size_t>(e);
    // if the next entry is uninitialized, update it with the next free index
    if (std::holds_alternative<std::monostate>(entries_[next_])) {
      entries_[next_] = next_ + 1;
    }
    e = std::move(reg);
    return index;
  }

  constexpr Reg* View(std::size_t index) noexcept override {
    auto& e = entries_[index];
    return std::holds_alternative<Reg>(e) ? &std::get<Reg>(e) : nullptr;
  }

  constexpr void Remove(std::size_t index) noexcept override {
    // swap indices and overwrite the entry with the next free index
    entries_[index] = next_;
    next_ = index;
  }

  constexpr std::size_t capacity() const noexcept override { return Capacity; }

 private:
  std::size_t next_{0};
  std::array<Entry, Capacity> entries_{};
};
}  // namespace ae::index_registry

#endif  // AETHER_INDEX_REGISTRY_DETAILS_INDEX_REGISTRY_LIST_H_
