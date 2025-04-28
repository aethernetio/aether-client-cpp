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

#ifndef AETHER_PTR_STORAGE_H_
#define AETHER_PTR_STORAGE_H_

#include <new>
#include <cstdint>
#include <utility>
#include <type_traits>

#include "aether/common.h"

namespace ae {
namespace storage_internal {
using DataPointerType = std::uint8_t*;
using DataPointerConstType = std::uint8_t const*;

struct Vtable {
  template <typename T>
  static Vtable* Create() {
    static Vtable vtable = []() {
      return Vtable{
          // init
          [](DataPointerType dst, DataPointerType data) {
            new (dst) T(std::move(*reinterpret_cast<T*>(data)));
          },
          // copy_construct
          [](DataPointerType dst, DataPointerConstType data) {
            new (dst) T(*reinterpret_cast<T const*>(data));
          },
          // copy_assign
          [](DataPointerType dst, DataPointerConstType data) {
            *reinterpret_cast<T*>(dst) = *reinterpret_cast<T const*>(data);
          },
          // move_construct
          [](DataPointerType dst, DataPointerType data) {
            new (dst) T(std::move(*reinterpret_cast<T*>(data)));
          },
          // move_assign
          [](DataPointerType dst, DataPointerType data) {
            *reinterpret_cast<T*>(dst) = std::move(*reinterpret_cast<T*>(data));
          },
          // destroy
          [](DataPointerType dst) { reinterpret_cast<T*>(dst)->~T(); },
      };
    }();
    return &vtable;
  }

  void (*init)(DataPointerType dst, DataPointerType data);
  void (*copy_construct)(DataPointerType dst, DataPointerConstType data);
  void (*copy_assign)(DataPointerType dst, DataPointerConstType data);
  void (*move_construct)(DataPointerType dst, DataPointerType data);
  void (*move_assign)(DataPointerType dst, DataPointerType data);
  void (*destroy)(DataPointerType dst);
};
}  // namespace storage_internal

template <std::size_t Size, std::size_t Align>
class SizedStorage {
 public:
  SizedStorage() noexcept = default;

  AE_CLASS_NO_COPY_MOVE(SizedStorage);

  template <typename T>
  [[nodiscard]] T const* ptr() const noexcept {
    return std::launder(reinterpret_cast<T const*>(data_));
  }

  template <typename T>
  [[nodiscard]] T* ptr() noexcept {
    return std::launder(reinterpret_cast<T*>(data_));
  }

  std::uint8_t* data() noexcept { return data_; }
  std::uint8_t const* data() const noexcept { return data_; }

 private:
  alignas(Align) std::uint8_t data_[Size];
};

template <std::size_t Size, std::size_t Align>
class CopyableStorage {
  using StorageType = SizedStorage<Size, Align>;

 public:
  CopyableStorage() noexcept : sized_storage_{}, vtable_{} {}

  template <typename T,
            AE_REQUIRERS_NOT((std::is_same<std::decay_t<T>, CopyableStorage>))>
  explicit CopyableStorage(T&& t) noexcept
      : sized_storage_{},
        vtable_{storage_internal::Vtable::template Create<T>()} {
    vtable_->init(sized_storage_.data(),
                  reinterpret_cast<storage_internal::DataPointerType>(&t));
  }

  ~CopyableStorage() {
    if (vtable_ != nullptr) {
      vtable_->destroy(sized_storage_.data());
    }
  }

  CopyableStorage(CopyableStorage const& other) noexcept
      : sized_storage_{}, vtable_{other.vtable_} {
    if (vtable_ != nullptr) {
      vtable_->copy_construct(sized_storage_.data(),
                              other.sized_storage_.data());
    }
  }

  CopyableStorage(CopyableStorage&& other) noexcept
      : sized_storage_{}, vtable_{other.vtable_} {
    if (vtable_ != nullptr) {
      vtable_->move_construct(sized_storage_.data(),
                              other.sized_storage_.data());
    }
  }

  CopyableStorage& operator=(CopyableStorage const& other) noexcept {
    if (this == &other) {
      return *this;
    }
    if (vtable_ != nullptr) {
      vtable_->destroy(sized_storage_.data());
    }
    vtable_ = other.vtable_;
    if (vtable_ != nullptr) {
      vtable_->copy_assign(sized_storage_.data(), other.sized_storage_.data());
    }
    return *this;
  }

  CopyableStorage& operator=(CopyableStorage&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    if (vtable_ != nullptr) {
      vtable_->destroy(sized_storage_.data());
    }
    vtable_ = other.vtable_;
    if (vtable_ != nullptr) {
      vtable_->move_assign(sized_storage_.data(), other.sized_storage_.data());
    }
    return *this;
  }

  template <typename T>
  [[nodiscard]] T const* ptr() const noexcept {
    return sized_storage_.template ptr<T>();
  }

  template <typename T>
  [[nodiscard]] T* ptr() noexcept {
    return sized_storage_.template ptr<T>();
  }

 private:
  StorageType sized_storage_;
  storage_internal::Vtable* vtable_;
};

template <typename T>
class Storage {
 public:
  Storage() noexcept = default;

  AE_CLASS_NO_COPY_MOVE(Storage);

  [[nodiscard]] T const* ptr() const noexcept {
    return sized_storage_.template ptr<T>();
  }
  [[nodiscard]] T* ptr() noexcept { return sized_storage_.template ptr<T>(); }

 private:
  SizedStorage<sizeof(T), alignof(T)> sized_storage_;
};
}  // namespace ae

#endif  // AETHER_PTR_STORAGE_H_
