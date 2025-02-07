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

#ifndef AETHER_PTR_PTR_VIEW_H_
#define AETHER_PTR_PTR_VIEW_H_

#include "aether/ptr/ptr.h"

namespace ae {

template <typename T>
class PtrView {
  template <typename U>
  friend class PtrView;

 public:
  PtrView() noexcept : ptr_storage_{} {}
  ~PtrView() { Reset(); }

  // Creation
  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  PtrView(Ptr<U> const& ptr) noexcept
      : ptr_storage_{ptr.ptr_storage_},
        ptr_{static_cast<T*>(ptr.ptr_)},
        visitor_func_{ptr.visitor_func_} {
    Increment();
  }

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  PtrView& operator=(Ptr<U> const& ptr) noexcept {
    Reset();
    ptr_storage_ = ptr.ptr_storage_;
    ptr_ = static_cast<T*>(ptr.ptr_);
    visitor_func_ = ptr.visitor_func_;
    Increment();
    return *this;
  }

  // Copying
  PtrView(PtrView const& other) noexcept
      : ptr_storage_{other.ptr_storage_},
        ptr_{other.ptr_},
        visitor_func_{other.visitor_func_} {
    Increment();
  }

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  PtrView(PtrView<U> const& other) noexcept
      : ptr_storage_{other.ptr_storage_},
        ptr_{static_cast<T*>(other.ptr_)},
        visitor_func_{other.visitor_func_} {
    Increment();
  }

  PtrView& operator=(PtrView const& other) noexcept {
    if (this != &other) {
      Reset();
      ptr_storage_ = other.ptr_storage_;
      ptr_ = other.ptr_;
      visitor_func_ = other.visitor_func_;
      Increment();
    }

    return *this;
  }

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  PtrView& operator=(PtrView<U> const& other) noexcept {
    if (this != &other) {
      Reset();
      ptr_storage_ = other.ptr_storage_;
      ptr_ = static_cast<T*>(other.ptr_);
      visitor_func_ = other.visitor_func_;
      Increment();
    }

    return *this;
  }

  // Moving
  PtrView(PtrView&& other) noexcept
      : ptr_storage_{other.ptr_storage_},
        ptr_{other.ptr_},
        visitor_func_{other.visitor_func_} {
    other.ptr_storage_ = nullptr;
  }

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  PtrView(PtrView<U>&& other) noexcept
      : ptr_storage_{other.ptr_storage_},
        ptr_{static_cast<T*>(other.ptr_)},
        visitor_func_{other.visitor_func_} {
    other.ptr_storage_ = nullptr;
  }

  PtrView& operator=(PtrView&& other) noexcept {
    if (this != &other) {
      Reset();
      std::swap(ptr_storage_, other.ptr_storage_);
      ptr_ = other.ptr_;
      visitor_func_ = other.visitor_func_;
    }
    return *this;
  }

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  PtrView& operator=(PtrView<U>&& other) noexcept {
    Reset();
    std::swap(ptr_storage_, other.ptr_storage_);
    ptr_ = static_cast<T*>(other.ptr_);
    visitor_func_ = other.visitor_func_;
    return *this;
  }

  // Access
  Ptr<T> Lock() const noexcept {
    if (operator bool()) {
      return Ptr<T>{ptr_storage_, ptr_, visitor_func_};
    }
    return Ptr<T>{nullptr};
  }

  explicit operator bool() const noexcept {
    return (ptr_storage_ != nullptr) ? ptr_storage_->ref_counters.main_refs != 0
                                     : false;
  }

  // Modify
  void Reset() noexcept {
    if (ptr_storage_ == nullptr) {
      return;
    }

    Decrement();

    if ((ptr_storage_->ref_counters.main_refs == 0) &&
        (ptr_storage_->ref_counters.weak_refs == 0)) {
      auto alloc = std::allocator<std::uint8_t>{};
      alloc.deallocate(reinterpret_cast<std::uint8_t*>(ptr_storage_),
                       static_cast<std::size_t>(ptr_storage_->alloc_size));
    }
    ptr_storage_ = nullptr;
  }

 private:
  void Increment() {
    if (ptr_storage_ == nullptr) {
      return;
    }
    ptr_storage_->ref_counters.weak_refs += 1;
  }

  void Decrement() {
    if (ptr_storage_ == nullptr) {
      return;
    }
    ptr_storage_->ref_counters.weak_refs -= 1;
  }

  PtrStorageBase* ptr_storage_;
  T* ptr_;
  typename Ptr<T>::VisitorFuncPtr visitor_func_;
};

template <typename U>
PtrView(Ptr<U> const& ptr) -> PtrView<U>;

}  // namespace ae

#endif  // AETHER_PTR_PTR_VIEW_H_
