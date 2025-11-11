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

#ifndef AETHER_TYPES_ALIGNED_STORAGE_H_
#define AETHER_TYPES_ALIGNED_STORAGE_H_

#include <new>
#include <cassert>
#include <cstdint>
#include <utility>
#include <cstring>
#include <type_traits>

#include "aether/common.h"

namespace ae {
/**
 * \brief The storage with specified alignment in stack.
 */
template <std::size_t Size, std::size_t Align>
class AlignedStorage {
 public:
  AlignedStorage() noexcept = default;

  template <typename T>
  [[nodiscard]] T const* ptr() const noexcept {
    static_assert(sizeof(T) <= Size, "T should fit into storage");
    static_assert(Align % alignof(T) == 0, "T should be aligned like Align");
    return std::launder(reinterpret_cast<T const*>(data_));
  }

  template <typename T>
  [[nodiscard]] T* ptr() noexcept {
    static_assert(sizeof(T) <= Size, "T should fit into storage");
    static_assert(Align % alignof(T) == 0, "T should be aligned like Align");
    return std::launder(reinterpret_cast<T*>(data_));
  }

  std::uint8_t* data() noexcept { return data_; }
  std::uint8_t const* data() const noexcept { return data_; }

 private:
  alignas(Align) std::uint8_t data_[Size];
};

using DataPointerType = std::uint8_t*;

enum class Operation : std::uint8_t {
  kCopy,
  kMove,
  kDestroy,
};

struct ManagedStorageVtable {
  void (*manage)(DataPointerType a, DataPointerType b, Operation op);
};

template <typename T>
static void ManageFunc([[maybe_unused]] DataPointerType a,
                       [[maybe_unused]] DataPointerType b,
                       Operation op) noexcept {
  switch (op) {
    case Operation::kCopy: {
      if constexpr (std::is_trivially_copy_constructible_v<T>) {
        std::memcpy(b, a, sizeof(T));
      } else if constexpr (std::is_copy_constructible_v<T>) {
        new (b) T(*std::launder(reinterpret_cast<const T*>(a)));
      } else {
        assert(false && "For copy operation, T must be copy constructible");
      }
      break;
    }
    case Operation::kMove: {
      if constexpr (std::is_trivially_move_constructible_v<T>) {
        std::memcpy(b, a, sizeof(T));
      } else if constexpr (std::is_move_constructible_v<T>) {
        new (b) T(std::move(*std::launder(reinterpret_cast<T*>(a))));
      } else {
        assert(false && "For move operation, T must be move constructible");
      }
      break;
    }
    case Operation::kDestroy: {
      if constexpr (!std::is_trivially_destructible_v<T>) {
        std::launder(reinterpret_cast<T*>(a))->~T();
      }
      break;
    }
  }
}

template <typename T>
static constexpr ManagedStorageVtable MSVTableForT = ManagedStorageVtable{
    ManageFunc<T>,
};

template <typename T>
struct InPlace {};

/**
 * \brief Managed storage with specified alignment in stack.
 * Maybe initialized with particular type T and erases it with custom vtable
 * for manage copy, move and destruction.
 */
template <std::size_t Size, std::size_t Align>
class ManagedStorage {
  using StorageType = AlignedStorage<Size, Align>;

 public:
  ManagedStorage() = default;

  // Create from instance of T
  template <typename T,
            AE_REQUIRERS_NOT((std::is_same<ManagedStorage, std::decay_t<T>>))>
  explicit ManagedStorage(T&& value) : vtable_{&MSVTableForT<std::decay_t<T>>} {
    static_assert(sizeof(T) <= Size, "T should fit into storage");
    static_assert(Align % alignof(T) == 0, "T should be aligned like Align");
    new (storage_.data()) T{std::forward<T>(value)};
  }

  // Create instance of T with arguments in place
  template <typename T, typename... TArgs>
  explicit ManagedStorage(InPlace<T>, TArgs&&... args)
      : vtable_{&MSVTableForT<T>} {
    static_assert(sizeof(T) <= Size, "T should fit into storage");
    static_assert(Align % alignof(T) == 0, "T should be aligned like Align");
    new (storage_.data()) T{std::forward<TArgs>(args)...};
  }

  // assign instance of T
  template <typename T,
            AE_REQUIRERS_NOT((std::is_same<ManagedStorage, std::decay_t<T>>))>
  ManagedStorage& operator=(T&& value) {
    static_assert(sizeof(T) <= Size, "T should fit into storage");
    static_assert(Align % alignof(T) == 0, "T should be aligned like Align");
    if (vtable_ != nullptr) {
      Destroy();
    }
    vtable_ = &MSVTableForT<std::decay_t<T>>;
    new (storage_.data()) T{std::forward<T>(value)};
    return *this;
  }

  ManagedStorage(ManagedStorage const& other) noexcept
      : vtable_{other.vtable_} {
    if (vtable_ != nullptr) {
      Copy(other.storage_, storage_);
    }
  }

  ManagedStorage(ManagedStorage&& other) noexcept : vtable_{other.vtable_} {
    if (vtable_ != nullptr) {
      Move(other.storage_, storage_);
      other.Destroy();
    }
    other.vtable_ = nullptr;
  }

  ~ManagedStorage() {
    if (vtable_ != nullptr) {
      Destroy();
    }
  }

  ManagedStorage& operator=(ManagedStorage const& other) noexcept {
    if (this != &other) {
      if (vtable_ != nullptr) {
        Destroy();
      }
      vtable_ = other.vtable_;
      if (vtable_ != nullptr) {
        Copy(other.storage_, storage_);
      }
    }
    return *this;
  }

  ManagedStorage& operator=(ManagedStorage&& other) noexcept {
    if (this != &other) {
      if (vtable_ != nullptr) {
        Destroy();
      }
      vtable_ = other.vtable_;
      if (vtable_ != nullptr) {
        Move(other.storage_, storage_);
        other.Destroy();
      }
      other.vtable_ = nullptr;
    }
    return *this;
  }

  template <typename T, typename... TArgs>
  void Emplace(TArgs&&... args) noexcept {
    static_assert(sizeof(T) <= Size, "T should fit into storage");
    static_assert(Align % alignof(T) == 0, "T should be aligned like Align");
    // destroy existing object if any
    if (vtable_ != nullptr) {
      Destroy();
    }
    vtable_ = &MSVTableForT<T>;
    new (storage_.data()) T{std::forward<TArgs>(args)...};
  }

  template <typename T>
  T* ptr() noexcept {
    return storage_.template ptr<T>();
  }

  template <typename T>
  T const* ptr() const noexcept {
    return storage_.template ptr<T>();
  }

 private:
  void Destroy() noexcept {
    vtable_->manage(storage_.data(), nullptr, Operation::kDestroy);
  }

  void Copy(StorageType const& src, StorageType& dst) noexcept {
    vtable_->manage(const_cast<std::uint8_t*>(src.data()), dst.data(),
                    Operation::kCopy);
  }

  void Move(StorageType& src, StorageType& dst) noexcept {
    vtable_->manage(src.data(), dst.data(), Operation::kMove);
  }

  ManagedStorageVtable const* vtable_{};
  StorageType storage_{};
};

/**
 * \brief Stack storage to fit specified type.
 */
template <typename T>
class Storage {
 public:
  Storage() noexcept = default;

  AE_CLASS_NO_COPY_MOVE(Storage);

  [[nodiscard]] T const* ptr() const noexcept {
    return aligned_storage_.template ptr<T>();
  }
  [[nodiscard]] T* ptr() noexcept { return aligned_storage_.template ptr<T>(); }

 private:
  AlignedStorage<sizeof(T), alignof(T)> aligned_storage_;
};
}  // namespace ae

#endif  // AETHER_TYPES_ALIGNED_STORAGE_H_
