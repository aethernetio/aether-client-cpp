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

#ifndef AETHER_API_PROTOCOL_DETAILS_PACKET_READER_H_
#define AETHER_API_PROTOCOL_DETAILS_PACKET_READER_H_

#include <cassert>
#include <optional>
#include <utility>

#include "aether-miscpp/serialization/binary_archive.h"

#include "aether/types/data_buffer.h"

#include "aether/api_protocol/details/api_message.h"

namespace ae {
class PacketReader {
 public:
  explicit PacketReader(DataBuffer const& buffer);

  /**
   * \brief Tries to extract a message from the reader.
   *
   * Returns nullopt on read failure without canceling or rolling back the
   * reader.
   */
  template <typename T>
  std::optional<T> TryExtract() {
    std::optional<T> result{std::in_place};
    if (auto res = archive.Load(result.value()); res.IsErr()) {
      return std::nullopt;
    }
    return result;
  }
  /**
   * \brief Extracts a message or part of one from the reader.
   *
   * The caller must know that a complete, correctly encoded T is present in
   * the buffer. If this precondition is violated, the reader is canceled and
   * no usable value is guaranteed.
   */
  template <typename T>
  T Extract() {
    T value{};
    if (auto res = archive.Load(value); res.IsErr()) {
      Cancel();
      assert(false && "value didn't extracted");
    }
    return value;
  }

  // cancel parsing
  void Cancel();
  bool canceled() const;
  bool eof() const;

 private:
  seri::BinaryArchive<MessageBuffer> archive;
  bool canceled_{false};
};
}  // namespace ae

#endif  // AETHER_API_PROTOCOL_DETAILS_PACKET_READER_H_
