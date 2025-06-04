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

#include "aether/domain_storage/static_domain_storage.h"

#include <cassert>
#include <iterator>
#include <algorithm>

namespace ae {
StaticDomainStorageReader::StaticDomainStorageReader(
    Span<std::uint8_t const> const& d)
    : data{&d}, offset{} {}

void StaticDomainStorageReader::read(void* out, std::size_t size) {
  assert((offset + size) <= data->size());
  std::copy(std::begin(*data) + offset, std::begin(*data) + offset + size,
            reinterpret_cast<std::uint8_t*>(out));
  offset += size;
}

ReadResult StaticDomainStorageReader::result() const {
  return ReadResult::kYes;
}

void StaticDomainStorageReader::result(ReadResult) {}
}  // namespace ae
