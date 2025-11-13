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

class PtrViewBase {
 public:
  PtrViewBase() noexcept : ptr_storage_{} {}
  ~PtrViewBase() { Reset(); }

  // Create/Assign
  explicit PtrViewBase(PtrBase const& ptr_base) noexcept;
  PtrViewBase& operator=(PtrBase const& ptr_base) noexcept;

  // Copy
  PtrViewBase(PtrViewBase const& other) noexcept;
  PtrViewBase& operator=(PtrViewBase const& other) noexcept;

  // Move
  PtrViewBase(PtrViewBase&& other) noexcept;
  PtrViewBase& operator=(PtrViewBase&& other) noexcept;

  explicit operator bool() const noexcept;

  void Reset() noexcept;

 protected:
  void Increment();
  void Decrement();

  PtrStorageBase* ptr_storage_;
};

template <typename T>
class PtrView : public PtrViewBase {
  template <typename U>
  friend class PtrView;

 public:
  PtrView() noexcept : PtrViewBase{} {}
  ~PtrView() = default;

  // Creation
  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  PtrView(Ptr<U> const& ptr) noexcept
      : PtrViewBase{static_cast<PtrBase const&>(ptr)},
        ptr_{static_cast<T*>(ptr.ptr_)} {}

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  PtrView& operator=(Ptr<U> const& ptr) noexcept {
    PtrViewBase::operator=(static_cast<PtrBase const&>(ptr));
    ptr_ = static_cast<T*>(ptr.ptr_);
    return *this;
  }

  // Copying
  PtrView(PtrView const& other) noexcept
      : PtrViewBase{other}, ptr_{other.ptr_} {}

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  PtrView(PtrView<U> const& other) noexcept
      : PtrViewBase{other}, ptr_{static_cast<T*>(other.ptr_)} {}

  PtrView& operator=(PtrView const& other) noexcept {
    if (this != &other) {
      PtrViewBase::operator=(other);
      ptr_ = other.ptr_;
    }

    return *this;
  }

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  PtrView& operator=(PtrView<U> const& other) noexcept {
    if (this != &other) {
      PtrViewBase::operator=(other);
      ptr_ = static_cast<T*>(other.ptr_);
    }

    return *this;
  }

  // Moving
  PtrView(PtrView&& other) noexcept
      : PtrViewBase{std::move(other)}, ptr_{other.ptr_} {}

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  PtrView(PtrView<U>&& other) noexcept
      : PtrViewBase{std::move(other)}, ptr_{static_cast<T*>(other.ptr_)} {}

  PtrView& operator=(PtrView&& other) noexcept {
    if (this != &other) {
      PtrViewBase::operator=(std::move(other));
      ptr_ = other.ptr_;
    }
    return *this;
  }

  template <typename U, AE_REQUIRERS((IsAbleToCast<T, U>))>
  PtrView& operator=(PtrView<U>&& other) noexcept {
    PtrViewBase::operator=(std::move(other));
    ptr_ = static_cast<T*>(other.ptr_);
    return *this;
  }

  using PtrViewBase::operator bool;

  // Access
  Ptr<T> Lock() const noexcept {
    if (operator bool()) {
      return Ptr<T>{ptr_, ptr_storage_};
    }
    return Ptr<T>{nullptr};
  }

 private:
  T* ptr_;
};

template <typename U>
PtrView(Ptr<U> const& ptr) -> PtrView<U>;

}  // namespace ae

#endif  // AETHER_PTR_PTR_VIEW_H_
