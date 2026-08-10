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

#include "aether/ptr/ptr.h"

namespace ae {
PtrBase::PtrBase() : ptr_storage_{nullptr} {}

PtrBase::PtrBase(PtrBase const& other) : PtrBase{other.ptr_storage_} {}

PtrBase::PtrBase(PtrBase&& other) noexcept
    : PtrBase{MoveTag{}, other.ptr_storage_} {
  other.ptr_storage_ = nullptr;
}

PtrBase::PtrBase(PtrStorageBase* ptr_storage) : ptr_storage_{ptr_storage} {
  Increment();
}

PtrBase::PtrBase(MoveTag, PtrStorageBase* ptr_storage)
    : ptr_storage_{ptr_storage} {
  // No Increment();
}

PtrBase& PtrBase::operator=(PtrBase const& other) {
  if (this != &other) {
    Reset();
    ptr_storage_ = other.ptr_storage_;
    Increment();
  }
  return *this;
}

PtrBase::operator bool() const noexcept {
  return (ptr_storage_ != nullptr) &&
         (ptr_storage_->ref_counters.main_refs != 0);
}

PtrBase& PtrBase::operator=(PtrBase&& other) noexcept {
  if (this != &other) {
    Reset();
    std::swap(ptr_storage_, other.ptr_storage_);
  }
  return *this;
}

void PtrBase::Reset() {
  if (!operator bool()) {
    return;
  }
  Decrement();
  if (ptr_storage_->ref_counters.main_refs == 0) {
    Destroy();
    if (ptr_storage_->ref_counters.weak_refs == 0) {
      Free();
    }
  }

  ptr_storage_ = nullptr;
}

void PtrBase::Increment() {
  if (ptr_storage_ == nullptr) {
    return;
  }
  assert((ptr_storage_->ref_counters.main_refs <
          std::numeric_limits<std::uint16_t>::max()) &&
         "Increment would overflow main_refs");
  assert((ptr_storage_->ref_counters.weak_refs <
          std::numeric_limits<std::uint16_t>::max()) &&
         "Increment would overflow weak_refs");
  ptr_storage_->ref_counters.main_refs += 1;
  ptr_storage_->ref_counters.weak_refs += 1;
}

void PtrBase::Decrement() {
  if (ptr_storage_ == nullptr) {
    return;
  }
  if (ptr_storage_->ref_counters.main_refs > 1) {
    auto count = DecrementGraphCount();
    DecrementRef(count);
  } else {
    DecrementRef();
  }
}

void PtrBase::DecrementRef(std::uint16_t count) {
  assert((ptr_storage_ != nullptr) && "DecrementRef but ptr_storage_ is null");
  assert((count <= ptr_storage_->ref_counters.main_refs) &&
         "DecrementRef main_refs underflow");
  assert((count <= ptr_storage_->ref_counters.weak_refs) &&
         "DecrementRef weak_refs underflow");
  ptr_storage_->ref_counters.main_refs -= count;
  ptr_storage_->ref_counters.weak_refs -= count;
}

std::uint16_t PtrBase::DecrementGraphCount() {
  assert((ptr_storage_ != nullptr) &&
         "DecrementGraphCount but ptr_storage_ is nullptr");

  RefTree ref_tree{};
  BuildDecrementGraph(ref_tree);

  // Check if current obj has no external references
  // External references may be direct or by children objects

  auto& self_node = ref_tree.get(0);
  assert((self_node.value.reachable_ref_count <
          std::numeric_limits<std::uint16_t>::max()) &&
         "reachable_ref_count overflow");
  // there is direct external references
  if (self_node.value.ref_count != self_node.value.reachable_ref_count) {
    // is not safe to delete completely
    return 1;
  }

  bool safe_to_delete = true;
  self_node.ForEach([&safe_to_delete](RefTree::Node& node) {
    // that node has external references
    // check if head is not reachable
    if (node.value.ref_count != node.value.reachable_ref_count) {
      if (node.IsReachable(0)) {
        safe_to_delete = false;
        return false;
      }
    }
    return true;
  });

  if (safe_to_delete) {
    return ptr_storage_->ref_counters.main_refs;
  }
  return 1;
}

void PtrBase::BuildDecrementGraph(RefTree& ref_tree) {
  auto& root = ref_tree.Emplace(reinterpret_cast<std::uintptr_t>(ptr_storage_),
                                ptr_storage_->ref_counters.main_refs);
  assert((root.value.reachable_ref_count <
          std::numeric_limits<std::uint16_t>::max()) &&
         "reachable_ref_count overflow");
  root.value.reachable_ref_count++;
  auto const root_index = root.index;
  BuildDecrementGraphImpl(this, ref_tree, root_index);
}

void PtrBase::BuildDecrementGraphImpl(PtrBase const* ptr, RefTree& tree,
                                      RefTree::Index head_index) {
  auto& head = tree.get(head_index);
  if (head.children_expanded) {
    return;
  }
  head.children_expanded = true;

  struct Arg {
    RefTree& tree;
    RefTree::Index head_index;
  };

  auto visit_children = [](void* arg, PtrBase const* child) {
    auto [tree, head_index] = *static_cast<Arg*>(arg);
    auto const pointer = reinterpret_cast<std::uintptr_t>(child->ptr_storage_);
    auto const ref_count = child->ptr_storage_->ref_counters.main_refs;
    auto& node = tree.Emplace(pointer, ref_count);
    assert((node.value.reachable_ref_count <
            std::numeric_limits<std::uint16_t>::max()) &&
           "reachable_ref_count overflow");
    node.value.reachable_ref_count++;
    auto const child_index = node.index;
    tree.get(head_index).PushChild(child_index);
    if (!node.children_expanded) {
      BuildDecrementGraphImpl(child, tree, child_index);
    }
  };

  auto arg = Arg{tree, head_index};

  ptr->ptr_storage_->manage_table->visit_children(ptr, &arg, visit_children);
}

void PtrBase::Destroy() {
  assert((ptr_storage_ != nullptr) && "Destroy but ptr_storage_ is nullptr");
  // prevent cycled ptrviews delete ptr_storage
  assert((ptr_storage_->ref_counters.weak_refs <
          std::numeric_limits<std::uint16_t>::max()) &&
         "Destroy would overflow weak_refs");
  ptr_storage_->ref_counters.weak_refs += 1;
  // call the destructor on pointer
  // Note if T is derived from Base, Base must have virtual ~Base to prevent
  // memory leaks
  ptr_storage_->manage_table->destroy(this);
  assert((ptr_storage_->ref_counters.weak_refs > 0) &&
         "Destroy would underflow weak_refs");
  ptr_storage_->ref_counters.weak_refs -= 1;
}

void PtrBase::Free() {
  auto alloc = std::allocator<std::uint8_t>{};
  alloc.deallocate(reinterpret_cast<std::uint8_t*>(ptr_storage_),
                   static_cast<std::size_t>(ptr_storage_->alloc_size));
}

bool operator==(PtrBase const& left, PtrBase const& right) {
  return left.ptr_storage_ == right.ptr_storage_;
}

bool operator!=(PtrBase const& left, PtrBase const& right) {
  return left.ptr_storage_ != right.ptr_storage_;
}

}  // namespace ae
