/*
 * Copyright 2025 Aethernet Inc.
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

#include "aether/ptr/ptr_view.h"

namespace ae {
PtrViewBase::PtrViewBase(PtrBase const& ptr_base) noexcept
    : ptr_storage_{ptr_base.ptr_storage_} {
  Increment();
}

PtrViewBase& PtrViewBase::operator=(PtrBase const& ptr_base) noexcept {
  Reset();
  ptr_storage_ = ptr_base.ptr_storage_;
  Increment();
  return *this;
}

PtrViewBase::PtrViewBase(PtrViewBase const& other) noexcept
    : ptr_storage_{other.ptr_storage_} {
  Increment();
}

PtrViewBase& PtrViewBase::operator=(PtrViewBase const& other) noexcept {
  if (this != &other) {
    Reset();
    ptr_storage_ = other.ptr_storage_;
    Increment();
  }
  return *this;
}

PtrViewBase::PtrViewBase(PtrViewBase&& other) noexcept
    : ptr_storage_{other.ptr_storage_} {
  other.ptr_storage_ = nullptr;
}

PtrViewBase& PtrViewBase::operator=(PtrViewBase&& other) noexcept {
  if (this != &other) {
    Reset();
    ptr_storage_ = other.ptr_storage_;
    other.ptr_storage_ = nullptr;
  }
  return *this;
}

PtrViewBase::operator bool() const noexcept {
  return (ptr_storage_ != nullptr) &&
         (ptr_storage_->ref_counters.main_refs != 0);
}

void PtrViewBase::Reset() noexcept {
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

void PtrViewBase::Increment() {
  if (ptr_storage_ == nullptr) {
    return;
  }
  ptr_storage_->ref_counters.weak_refs += 1;
}

void PtrViewBase::Decrement() {
  if (ptr_storage_ == nullptr) {
    return;
  }
  ptr_storage_->ref_counters.weak_refs -= 1;
}

}  // namespace ae
