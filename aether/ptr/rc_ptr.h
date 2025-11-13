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

#ifndef AETHER_PTR_RC_PTR_H_
#define AETHER_PTR_RC_PTR_H_

#include <memory>
#include <cstdint>
#include <cassert>
#include <utility>
#include <atomic>

#include "aether/mstream.h"
#include "aether/types/aligned_storage.h"

namespace ae {
// for most cases 2*uint8_t should be more than enough
// saving more space is impossible due to alignment in RcStorage
struct RefCounters {
  std::atomic<std::uint8_t> main_refs;
  std::atomic<std::uint8_t> weak_refs;
};

template <typename T>
struct RcStorage {
 public:
  [[nodiscard]] auto* ptr() noexcept { return storage.ptr(); }
  [[nodiscard]] auto* ptr() const noexcept { return storage.ptr(); }

  RefCounters ref_counters;
  Storage<T> storage;
};

template <typename T>
class RcPtr {
  template <typename U>
  friend class RcPtr;

  template <typename U>
  friend class RcPtrView;

 public:
  RcPtr() noexcept : rc_storage_{nullptr} {}
  RcPtr(std::nullptr_t) noexcept : rc_storage_{nullptr} {}
  explicit RcPtr(RcStorage<T>* rc_storage) noexcept : rc_storage_{rc_storage} {
    Increment();
  }
  ~RcPtr() { Reset(); }

  // Copying
  RcPtr(RcPtr const& other) noexcept : RcPtr{other.rc_storage_} {}

  auto& operator=(RcPtr const& other) noexcept {
    if (this != &other) {
      Reset();
      rc_storage_ = other.rc_storage_;
      Increment();
    }
    return *this;
  }

  // Moving
  RcPtr(RcPtr&& other) noexcept : rc_storage_{other.rc_storage_} {
    other.rc_storage_ = nullptr;
  }

  auto& operator=(RcPtr&& other) noexcept {
    if (this != &other) {
      Reset();
      std::swap(rc_storage_, other.rc_storage_);
    }
    return *this;
  }

  // Access
  [[nodiscard]] T* get() {
    return (rc_storage_ != nullptr) ? rc_storage_->ptr() : nullptr;
  }
  [[nodiscard]] T* get() const {
    return (rc_storage_ != nullptr) ? rc_storage_->ptr() : nullptr;
  }

  [[nodiscard]] T* operator->() noexcept {
    assert((rc_storage_ != nullptr) && "Dereferencing a null pointer");
    return get();
  }
  [[nodiscard]] T* operator->() const noexcept {
    assert((rc_storage_ != nullptr) && "Dereferencing a null pointer");
    return get();
  }
  [[nodiscard]] T& operator*() noexcept {
    assert((rc_storage_ != nullptr) && "Dereferencing a null pointer");
    return *get();
  }
  [[nodiscard]] T& operator*() const noexcept {
    assert((rc_storage_ != nullptr) && "Dereferencing a null pointer");
    return *get();
  }

  explicit operator bool() const noexcept { return rc_storage_ != nullptr; }

  // Modifying
  void Reset() {
    if (rc_storage_ == nullptr) {
      return;
    }

    Decrement();
    if (rc_storage_->ref_counters.main_refs == 0) {
      Destroy();
      if (rc_storage_->ref_counters.weak_refs == 0) {
        Free();
      }
    }
    rc_storage_ = nullptr;
  }

 private:
  void Decrement() noexcept { rc_storage_->ref_counters.main_refs -= 1; }
  void Increment() noexcept {
    if (rc_storage_ == nullptr) {
      return;
    }
    rc_storage_->ref_counters.main_refs += 1;
  }

  void Destroy() {
    // prevent cycled rcptrviews delete rc_storage
    rc_storage_->ref_counters.weak_refs += 1;
    // Call destructor on T
    rc_storage_->ptr()->~T();
    rc_storage_->ref_counters.weak_refs -= 1;
  }

  void Free() noexcept {
    auto alloc = std::allocator<RcStorage<T>>{};
    alloc.deallocate(rc_storage_, std::size_t{1});
  }

  RcStorage<T>* rc_storage_;
};

/**
 * \brief Construct an RcPtr
 */
template <typename T, typename... TArgs>
auto MakeRcPtr(TArgs&&... args) noexcept {
  auto alloc = std::allocator<RcStorage<T>>{};
  auto* rc_storage = alloc.allocate(std::size_t{1});
  assert((rc_storage != nullptr) && "Bad alloc!");
  [[maybe_unused]] auto* constructed =
      new (rc_storage->ptr()) T{std::forward<TArgs>(args)...};
  assert(constructed != nullptr && "Construction failed!");
  rc_storage->ref_counters.main_refs = 0;
  rc_storage->ref_counters.weak_refs = 0;
  return RcPtr<T>{rc_storage};
}

template <template <typename...> typename T, typename... TArgs>
auto MakeRcPtr(TArgs&&... args) noexcept {
  using DeducedT = decltype(T{std::forward<TArgs>(args)...});
  return MakeRcPtr<DeducedT>(std::forward<TArgs>(args)...);
}

template <typename T>
class RcPtrView {
  template <typename U>
  friend class RcPtrView;

  using OurRcPtr = RcPtr<T>;

 public:
  RcPtrView() noexcept : rc_storage_{nullptr} {}
  ~RcPtrView() noexcept { Reset(); }

  RcPtrView(OurRcPtr const& rpc_ptr) noexcept
      : RcPtrView{rpc_ptr.rc_storage_} {}

  // Copying
  RcPtrView(RcPtrView const& other) noexcept : RcPtrView{other.rc_storage_} {}

  RcPtrView& operator=(RcPtrView const& other) noexcept {
    if (this != &other) {
      Reset();
      rc_storage_ = other.rc_storage_;
      Increment();
    }
    return *this;
  }

  // Moving
  RcPtrView(RcPtrView&& other) noexcept : rc_storage_{other.rc_storage_} {
    other.rc_storage_ = nullptr;
  }

  RcPtrView& operator=(RcPtrView&& other) noexcept {
    if (this != &other) {
      Reset();
      std::swap(rc_storage_, other.rc_storage_);
    }
    return *this;
  }

  // Access
  [[nodiscard]] OurRcPtr lock() noexcept {
    if (rc_storage_ != nullptr) {
      if (rc_storage_->ref_counters.main_refs != 0) {
        return OurRcPtr{rc_storage_};
      }
    }
    return OurRcPtr{};
  }

  [[nodiscard]] OurRcPtr lock() const noexcept {
    if (rc_storage_ != nullptr) {
      if (rc_storage_->ref_counters.main_refs != 0) {
        return OurRcPtr{rc_storage_};
      }
    }
    return OurRcPtr{};
  }

  explicit operator bool() const noexcept {
    return (rc_storage_ != nullptr) &&
           (rc_storage_->ref_counters.main_refs != 0);
  }

  // Modifying
  void Reset() noexcept {
    if (rc_storage_ == nullptr) {
      return;
    }

    Decrement();
    if ((rc_storage_->ref_counters.main_refs == 0) &&
        (rc_storage_->ref_counters.weak_refs == 0)) {
      Free();
    }
    rc_storage_ = nullptr;
  }

 private:
  explicit RcPtrView(RcStorage<T>* rc_storage) noexcept
      : rc_storage_{rc_storage} {
    Increment();
  }

  void Decrement() noexcept { rc_storage_->ref_counters.weak_refs -= 1; }
  void Increment() noexcept {
    if (rc_storage_ == nullptr) {
      return;
    }
    rc_storage_->ref_counters.weak_refs += 1;
  }
  void Free() noexcept {
    auto alloc = std::allocator<RcStorage<T>>{};
    alloc.deallocate(rc_storage_, std::size_t{1});
  }

  RcStorage<T>* rc_storage_;
};

template <typename T, typename Ob>
omstream<Ob>& operator<<(omstream<Ob>& s, RcPtr<T> const& v) {
  if (v) {
    s << true;
    s << *v;
  } else {
    s << false;
  }
  return s;
}

template <typename T, typename Ib>
imstream<Ib>& operator>>(imstream<Ib>& s, RcPtr<T>& v) {
  bool has_value{};
  s >> has_value;
  if (!data_was_read(s)) {
    return s;
  }
  if (has_value) {
    auto temp = MakeRcPtr<T>();
    s >> *temp;
    if (data_was_read(s)) {
      v = std::move(temp);
    }
  }
  return s;
}

}  // namespace ae

#endif  // AETHER_PTR_RC_PTR_H_
