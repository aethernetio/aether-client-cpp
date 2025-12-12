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

void PtrBase::DecrementRef(std::uint8_t count) {
  ptr_storage_->ref_counters.main_refs -= count;
  ptr_storage_->ref_counters.weak_refs -= count;
}

std::uint8_t PtrBase::DecrementGraphCount() {
  assert((ptr_storage_ != nullptr) &&
         "DecrementGraphCount but ptr_storage_ is nullptr");

  auto ref_tree = BuildDecrementGraph();

  // Check if current obj has no external references
  // External references may be direct or by children objects

  auto& self_node = ref_tree.get(0);
  // there is direct external references
  if (self_node.value.ref_count != self_node.value.reachable_ref_count) {
    // is not safe to delete completely
    return 1;
  }

  bool safe_to_delete = true;
  self_node.ForEach([&safe_to_delete](RefTree::Node& node) {
    // that node has external references
    // check if head not is reachable
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

RefTree PtrBase::BuildDecrementGraph() {
  RefTree tree;
  auto& node = tree.Emplace(reinterpret_cast<std::uintptr_t>(ptr_storage_),
                            ptr_storage_->ref_counters.main_refs);
  node.value.reachable_ref_count++;
  std::set<std::uintptr_t> visited;
  visited.insert(reinterpret_cast<std::uintptr_t>(ptr_storage_));
  BuildDecrementGraphImpl(tree, node.index, Children(), visited);
  return tree;
}

void PtrBase::BuildDecrementGraphImpl(
    RefTree& tree, RefTree::Index head_index,
    std::vector<PtrBase const*> const& children,
    std::set<std::uintptr_t>& visited) {
  for (auto const* child : children) {
    auto& node =
        tree.Emplace(reinterpret_cast<std::uintptr_t>(child->ptr_storage_),
                     child->ptr_storage_->ref_counters.main_refs);
    node.value.reachable_ref_count++;
    tree.get(head_index).children_indices.push_back(node.index);

    // cycle detection
    auto [_, not_visited] =
        visited.emplace(reinterpret_cast<std::uintptr_t>(child->ptr_storage_));
    if (not_visited) {
      BuildDecrementGraphImpl(tree, node.index, child->Children(), visited);
    }
  }
}

std::vector<PtrBase const*> PtrBase::Children() const {
  if (ptr_storage_ == nullptr) {
    return {};
  }
  if (ptr_storage_->ref_counters.main_refs == 0) {
    return {};
  }
  return ptr_storage_->manage_table->child_ptrs(this);
}

void PtrBase::Destroy() {
  assert((ptr_storage_ != nullptr) && "Destroy but ptr_storage_ is nullptr");
  // prevent cycled ptrviews delete ptr_storage
  ptr_storage_->ref_counters.weak_refs += 1;
  // call the destructor on pointer
  // Note if T is derived from Base, Base must have virtual ~Base to prevent
  // memory leaks
  ptr_storage_->manage_table->destroy(this);
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
