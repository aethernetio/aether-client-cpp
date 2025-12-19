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

#include "aether/obj/domain.h"

namespace ae {

Domain::Domain(IDomainStorage& storage) : storage_{&storage} {}

std::unique_ptr<IDomainStorageReader> Domain::GetReader(DataKey key,
                                                        std::uint8_t version) {
  auto load = storage_->Load(key, version);
  if ((load.result == DomainLoadResult::kEmpty) ||
      (load.result == DomainLoadResult::kRemoved)) {
    return {};
  }
  assert(load.reader && "Reader must be created!");
  return std::move(load.reader);
}

std::unique_ptr<IDomainStorageWriter> Domain::GetWriter(DataKey key,
                                                        std::uint8_t version) {
  auto writer = storage_->Store(key, version);
  assert(writer && "Writer must be created!");
  return writer;
}
}  // namespace ae
