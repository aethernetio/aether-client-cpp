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
#include <cstddef>
#include <cstring>

namespace ae {
StaticDomainStorageReader::StaticDomainStorageReader(
    Span<std::uint8_t const> const& d)
    : data_buffer(d.data(), d.size()) {}

seri::SeriResult StaticDomainStorageReader::Read(seri::SizeReadTag data) {
  std::uint32_t u_size{};
  TRY_RESULT(Read(seri::DataTag{u_size}));
  data.size = static_cast<std::size_t>(u_size);
  return Ok{seri::good};
}

seri::SeriResult StaticDomainStorageReader::Read(seri::DataReadTag data) {
  if (data_buffer.size() < data.size) {
    return Error{seri::read_eof};
  }
  std::memcpy(data.data, data_buffer.data(), data.size);
  data_buffer = data_buffer.subspan(data.size);
  return Ok{seri::good};
}
}  // namespace ae
