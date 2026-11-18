/*
 * Copyright 2026 Aethernet Inc.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * http://www.apache.org/licenses/LICENSE-2.0
 */

#ifndef AETHER_TYPES_PING_STATISTICS_H_
#define AETHER_TYPES_PING_STATISTICS_H_

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "ae-numeric/exponential.h"
#include "ae-numeric/integer_math.h"
#include "ae-numeric/tiered_int_view.h"

#include "aether-miscpp/serialization/binary_archive.h"

#include "aether/clock.h"
#include "aether/types/presence_numeric.h"

namespace ae {

template <std::size_t Capacity>
class PingStatistics {
  template <typename A, typename T, typename>
  friend struct seri::Serializer;

  // Cover the full uint32_t microsecond range, including late responses.
  using Runtime = FixedPoint<std::uint32_t, 4295.0>;
  using Wire = TieredInt<std::uint8_t, 254>;
  using Sample = Exponential<Runtime, Wire, 0.001, 4295.0, 510>;
  using Code = Sample::wire_value_type;
  static_assert(sizeof(Code) == 2);
  static_assert(Capacity > 0);

  static constexpr std::uint32_t kUsPerSecond = 1'000'000;
  static constexpr std::uint32_t kRawPerSecond = 1U << Runtime::kFractionBits;
  static constexpr std::size_t kByteCapacity = Capacity * Wire::kMaxWireBytes;

 public:
  void Add(Duration duration) {
    std::uint32_t raw = 0;
    // The scale of Runtime covers every valid Duration, so this cannot
    // overflow.
    if (!integer_math::MulDivU32Nearest(duration.count(), kRawPerSecond,
                                        kUsPerSecond, raw)) {
      raw = Runtime::kRawMax;
    }
    auto const code = Sample::FromRuntime(Runtime::FromRaw(raw)).WireCode();
    if (size_ == Capacity) {
      auto const first_size = Wire::WireBytesNeeded(bytes_.data(), used_);
      used_ -= first_size;
      std::memmove(bytes_.data(), bytes_.data() + first_size, used_);
      --size_;
    }
    used_ += code.Serialize(bytes_.data() + used_);
    ++size_;
  }

  [[nodiscard]] std::size_t size() const { return size_; }
  [[nodiscard]] bool empty() const { return size_ == 0; }
  [[nodiscard]] Duration min() const { return Select(0); }
  [[nodiscard]] Duration max() const { return Select(size_ - 1); }

  template <std::size_t P>
  [[nodiscard]] Duration percentile() const {
    static_assert(P <= 100);
    return PercentileValue(P);
  }

  [[nodiscard]] Duration PercentileValue(std::size_t percentile) const {
    assert(percentile <= 100 && "Percentile must be in [0, 100]");
    return Select(PercentileIndexInteger(size_, percentile));
  }

  [[nodiscard]] Duration PercentileValue(Percentile percentile) const {
    return Select(PercentileIndex(size_, percentile));
  }

 private:
  [[nodiscard]] Duration Select(std::size_t index) const {
    assert(index < size_ && "Ping statistics require a nonempty window");
    std::array<Code, Capacity> codes;
    auto* const first = codes.data();
    auto* last = first;
    for (auto code : MakeTieredIntView<Wire>({bytes_.data(), used_})) {
      *last++ = static_cast<Code>(code);
    }
    auto* const selected = first + index;
    std::nth_element(first, selected, last);
    if (*selected == Sample::kBoundaryCode) {
      return Duration::max();
    }
    auto const raw = Sample::FromCode(Wire{*selected}).ToRuntime().RawValue();
    if (std::uint32_t us = 0;
        integer_math::MulDivU32Nearest(raw, kUsPerSecond, kRawPerSecond, us)) {
      return Duration{us};
    }
    return Duration::max();
  }

  std::array<std::uint8_t, kByteCapacity> bytes_{};
  std::size_t used_ = 0;
  std::size_t size_ = 0;
};

namespace seri {
template <BinaryBuffer B, std::size_t Capacity>
struct Serializer<BinaryArchive<B>, PingStatistics<Capacity>> {
  using Counter = PingStatistics<Capacity>;
  using Wire = typename Counter::Wire;
  using Sample = typename Counter::Sample;

  SeriResult Seri(BinaryArchive<B>& archive, Meta<Counter const> meta) const {
    TRY_RESULT(archive.buffer().Write(SizeTag{meta.value.used_}));
    return archive.buffer().Write(
        DataWriteTag{meta.value.bytes_.data(), meta.value.used_});
  }

  SeriResult Deseri(BinaryArchive<B>& archive, Meta<Counter> meta) const {
    Counter restored;
    TRY_RESULT(archive.buffer().Read(SizeTag{restored.used_}));
    if (restored.used_ > restored.bytes_.size()) {
      return Error{read_eof};
    }
    TRY_RESULT(archive.buffer().Read(
        DataReadTag{restored.bytes_.data(), restored.used_}));
    std::size_t offset = 0;
    while (offset < restored.used_) {
      Wire code;
      auto const consumed = code.Deserialize(restored.bytes_.data() + offset,
                                             restored.used_ - offset);
      if (consumed == 0 ||
          static_cast<typename Counter::Code>(code) > Sample::kBoundaryCode ||
          restored.size_ == Capacity) {
        return Error{read_eof};
      }
      offset += consumed;
      ++restored.size_;
    }
    meta.value = restored;
    return Ok{good};
  }
};
}  // namespace seri

}  // namespace ae

#endif  // AETHER_TYPES_PING_STATISTICS_H_
