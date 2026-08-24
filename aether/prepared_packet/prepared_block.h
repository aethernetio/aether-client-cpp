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

#ifndef AETHER_PREPARED_PACKET_PREPARED_BLOCK_H_
#define AETHER_PREPARED_PACKET_PREPARED_BLOCK_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

#include "aether-miscpp/serialization/binary_archive.h"
#include "aether/types/packed_size.h"

namespace ae::prepared_packet {
// magic value to indicate block is valid and contain no garbage
static constexpr std::uint32_t kMagic = 0x50534456;  // "PSDV"

// Raw block with raw_data is needed because we could store only trivial types
// as RTC_DATA
template <std::size_t Size>
struct RawBlock {
  std::uint32_t magic;
  std::array<std::uint8_t, Size> raw_data;
};

template <typename T, std::size_t MaxSize = sizeof(T)>
struct PreparedBlock {
  /**
   * \brief RAII access to T value.
   * after usage updated value will be retained in the prepared block
   */
  struct PreparedProxy {
    PreparedProxy(PreparedProxy&&) noexcept = delete;
    PreparedProxy(PreparedProxy const&) noexcept = delete;
    PreparedProxy& operator=(PreparedProxy&&) noexcept = delete;
    PreparedProxy& operator=(PreparedProxy const&) noexcept = delete;

    constexpr explicit PreparedProxy(T&& v, PreparedBlock& h) noexcept(
        std::is_nothrow_move_constructible_v<T>)
        : value{std::move(v)}, host{&h} {}

    constexpr ~PreparedProxy() noexcept(std::is_nothrow_destructible_v<T>) {
      host->Retain(std::move(value));
    }

    constexpr T& operator*() noexcept { return value; }
    constexpr T const& operator*() const noexcept { return value; }
    constexpr T* operator->() noexcept { return &value; }
    constexpr T const* operator->() const noexcept { return &value; }
    constexpr T* operator&() noexcept { return &value; }
    constexpr T const* operator&() const noexcept { return &value; }
    constexpr explicit operator T() noexcept { return value; }
    constexpr explicit operator T() const noexcept { return value; }

    T value;
    PreparedBlock* host;
  };

  PreparedProxy Resolve();
  void Retain(T&& value);

  constexpr bool is_valid() const { return raw.magic == kMagic; }

  RawBlock<MaxSize> raw;
};

namespace prepared_block_internal {
struct SpanBuffer {
  seri::SeriResult Write(seri::SizeWriteTag size) {
    // write into temp buffer first, then actually write to the main buff
    auto psize = PackedSize{size.size};
    auto buff = std::array<std::uint8_t, PackedSize::kMaxWireBytes>{};
    auto seri_size = ae::Serialize(psize, buff.data());
    return Write(seri::DataWriteTag{buff.data(), seri_size});
  }
  seri::SeriResult Write(seri::DataWriteTag data) {
    if ((pos + data.size) > buffer.size()) {
      return Error{seri::write_eof};
    }
    std::memcpy(buffer.data() + pos, data.data, data.size);
    pos += data.size;
    return Ok{seri::good};
  }

  seri::SeriResult Read(seri::SizeReadTag size) {
    auto res =
        ae::Deserialize<PackedSize>(buffer.data() + pos, buffer.size() - pos);
    if (res.bytes_read == 0) {
      return Error{seri::read_error};
    }
    pos += res.bytes_read;
    size.size = static_cast<std::size_t>(res.value);
    return Ok{seri::good};
  }
  seri::SeriResult Read(seri::DataReadTag data) {
    if ((pos + data.size) > buffer.size()) {
      return Error{seri::read_eof};
    }
    std::memcpy(data.data, buffer.data() + pos, data.size);
    pos += data.size;
    return Ok{seri::good};
  }

  std::span<std::uint8_t> buffer;
  std::size_t pos;
};
}  // namespace prepared_block_internal

template <typename T, std::size_t MaxSize>
PreparedBlock<T, MaxSize>::PreparedProxy PreparedBlock<T, MaxSize>::Resolve() {
  auto archive = seri::BinaryArchive{
      prepared_block_internal::SpanBuffer{.buffer = raw.raw_data, .pos = {}}};
  T v{};
  archive.Load(v);
  return PreparedProxy{std::move(v), *this};
}

template <typename T, std::size_t MaxSize>
void PreparedBlock<T, MaxSize>::Retain(T&& value) {
  auto archive = seri::BinaryArchive{
      prepared_block_internal::SpanBuffer{.buffer = raw.raw_data, .pos = {}}};
  auto&& v = std::move(value);
  [[maybe_unused]] auto res = archive.Save(v);
  assert(!!res && "Object should be saved in archive");
  raw.magic = kMagic;
}

}  // namespace ae::prepared_packet

#endif  // AETHER_PREPARED_PACKET_PREPARED_BLOCK_H_
