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

#ifndef AETHER_INDEX_REGISTRY_INDEX_REGISTRY_H_
#define AETHER_INDEX_REGISTRY_INDEX_REGISTRY_H_

#include "aether/index_registry/details/indexed_context.h"
#include "aether/index_registry/details/index_registry_list.h"

namespace ae {
template <std::size_t Capacity>
using IndexRegistryList = index_registry::IndexRegistryList<Capacity>;
template <typename T>
using IndexCtx = index_registry::IndexCtx<T>;
template <typename T>
using IndexCtxView = index_registry::IndexCtxView<T>;

using AliveContext = index_registry::IndexCtx<void>;
using AliveContextView = index_registry::IndexCtxView<void>;

}  // namespace ae

#endif  // AETHER_INDEX_REGISTRY_INDEX_REGISTRY_H_
