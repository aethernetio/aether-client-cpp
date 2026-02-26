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

#include "aether/api_protocol/child_data.h"

namespace ae {
ChildData::ChildData() = default;

ChildData::ChildData(std::vector<std::uint8_t> data)
    : pack_data_{std::move(data)} {}
ChildData::ChildData(std::unique_ptr<IPackMessage> pack_message)
    : pack_data_{std::move(pack_message)} {}

std::vector<std::uint8_t> ChildData::PackData(
    ProtocolContext& protocol_context) && {
  if (pack_data_.index() == 0) {
    return DataPackMessage(protocol_context,
                           std::move(*std::get<0>(pack_data_)));
  }
  return std::get<1>(pack_data_);
}

std::vector<std::uint8_t> const& ChildData::PackData() const {
  if (pack_data_.index() == 0) {
    assert(false);
  }
  return std::get<1>(pack_data_);
}

std::vector<std::uint8_t> ChildData::DataPackMessage(
    ProtocolContext& protocol_context, IPackMessage&& pack_message) {
  std::vector<std::uint8_t> res;
  auto packer = ApiPacker{protocol_context, res};
  std::move(pack_message).Pack(packer);
  return res;
}

}  // namespace ae
