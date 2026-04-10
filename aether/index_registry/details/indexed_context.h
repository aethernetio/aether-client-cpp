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

#ifndef AETHER_INDEX_REGISTRY_DETAILS_INDEXED_CONTEXT_H_
#define AETHER_INDEX_REGISTRY_DETAILS_INDEXED_CONTEXT_H_

#include <cassert>
#include <cstdint>

#include "aether/index_registry/details/index_registry_list.h"

namespace ae::index_registry {
// something that holds a reference to an index registry
template <typename T>
concept CtxRegistry = requires(T const& t) {
  { t.index_registry() } -> std::convertible_to<IndexRegistryListBase&>;
};

template <typename T>
class IndexCtxView;

template <typename T>
class IndexCtx {
  friend class IndexCtxView<T>;

 public:
  constexpr IndexCtx(IndexRegistryListBase& registry_list, T* ptr) noexcept
      : registry_list_{&registry_list}, index_{registry_list_->Put(Reg{ptr})} {
    assert(index_ != registry_list_->capacity() &&
           "Registry returns invalid index");
  }
  template <CtxRegistry Cr>
  constexpr IndexCtx(Cr const& ctx_registry, T* ptr) noexcept
      : registry_list_{&ctx_registry.index_registry()},
        index_{registry_list_->Put(Reg{ptr})} {
    assert(index_ != registry_list_->capacity() &&
           "Registry returns invalid index");
  }
  constexpr ~IndexCtx() noexcept { registry_list_->Remove(index_); }

  IndexCtxView<T> View() const noexcept;

 private:
  IndexRegistryListBase* registry_list_;
  std::size_t index_;
};

template <>
class IndexCtx<void> {
  friend class IndexCtxView<void>;

 public:
  constexpr explicit IndexCtx(IndexRegistryListBase& registry_list) noexcept
      : registry_list_{&registry_list}, index_{registry_list_->Put(Reg{this})} {
    assert(index_ != registry_list_->capacity() &&
           "Registry returns invalid index");
  }
  template <CtxRegistry Cr>
  constexpr explicit IndexCtx(Cr const& ctx_registry) noexcept
      : registry_list_{&ctx_registry.index_registry()},
        index_{registry_list_->Put(Reg{nullptr})} {
    assert(index_ != registry_list_->capacity() &&
           "Registry returns invalid index");
  }

  constexpr ~IndexCtx() noexcept { registry_list_->Remove(index_); }

  IndexCtxView<void> View() const noexcept;

 private:
  IndexRegistryListBase* registry_list_;
  std::size_t index_;
};

template <typename T>
class IndexCtxView {
 public:
  constexpr explicit IndexCtxView(IndexCtx<T> const& ctx) noexcept
      : registry_list_{ctx.registry_list_},
        index_{ctx.index_},
        fngr_print_{(registry_list_->View(index_) != nullptr)
                        ? reinterpret_cast<std::uintptr_t>(
                              registry_list_->View(index_)->ptr)
                        : 0} {}

  explicit constexpr operator bool() const noexcept {
    auto* view = registry_list_->View(index_);
    return (view != nullptr) &&
           (fngr_print_ == reinterpret_cast<std::uintptr_t>(view->ptr));
  }

  constexpr T* get() const noexcept {
    auto* view = registry_list_->View(index_);
    return (view != nullptr) ? static_cast<T*>(view->ptr) : nullptr;
  }

  constexpr T* operator->() const noexcept { return get(); }

 private:
  IndexRegistryListBase* registry_list_;
  std::size_t index_;
  std::uintptr_t fngr_print_;
};

template <>
class IndexCtxView<void> {
 public:
  constexpr explicit IndexCtxView(IndexCtx<void> const& ctx) noexcept
      : registry_list_{ctx.registry_list_},
        index_{ctx.index_},
        fngr_print_{(registry_list_->View(index_) != nullptr)
                        ? reinterpret_cast<std::uintptr_t>(
                              registry_list_->View(index_)->ptr)
                        : 0} {}

  explicit constexpr operator bool() const noexcept {
    auto* view = registry_list_->View(index_);
    return (view != nullptr) &&
           (fngr_print_ == reinterpret_cast<std::uintptr_t>(view->ptr));
  }

 private:
  IndexRegistryListBase* registry_list_;
  std::size_t index_;
  std::uintptr_t fngr_print_;
};

template <typename T>
IndexCtxView<T> IndexCtx<T>::View() const noexcept {
  return IndexCtxView<T>{*this};
}

}  // namespace ae::index_registry

#endif  // AETHER_INDEX_REGISTRY_DETAILS_INDEXED_CONTEXT_H_
