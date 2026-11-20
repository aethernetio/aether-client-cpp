/*
 * Copyright 2026 Aethernet Inc.
 *
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef AETHER_ADAPTIVE_DATAGRAM_ADAPTIVE_DATAGRAM_TIME_H_
#define AETHER_ADAPTIVE_DATAGRAM_ADAPTIVE_DATAGRAM_TIME_H_

#include <cstdint>
#include <limits>

#include "aether/types/packed_size.h"

namespace ae::adaptive_datagram {
struct DatagramTiming {
  std::uint64_t queued_at_us{};
  std::uint64_t sent_at_us{};
  std::uint32_t lifetime_us{};
};

struct FeedbackTiming {
  std::uint64_t echo_sent_at_us{};
  std::uint64_t received_at_us{};
  std::uint64_t reported_at_us{};
};

namespace adaptive_datagram_time_internal {
// Reuse the SafeStream delta codec's 1/2/4-byte instantiation. These are
// time codes (microseconds plus flags), independent of its byte window.
using TimeCode = TieredInt<std::uint8_t, 215, 10359>;
inline constexpr std::uint32_t kAbsolute = 2;
inline constexpr std::uint32_t kScale[] = {1,     10,     100,    1000,
                                           10000, 100000, 1000000};

// Bits 0 and 1 select negative and decimal-scaled deltas. An unscaled value
// holds integer microseconds. In a scaled value the next three bits are the
// power of ten; only exact factors are removed. An escape carries a full
// absolute value, including when clocks on the two peers are far apart.
inline TimeCode EncodeTime(std::uint64_t anchor, std::uint64_t value) {
  auto const negative = value < anchor;
  auto magnitude = negative ? anchor - value : value - anchor;
  if (magnitude == 0) {
    return TimeCode{0};
  }
  unsigned exponent = 0;
  while (exponent < 6 && magnitude % 10 == 0) {
    magnitude /= 10;
    ++exponent;
  }
  auto const factor = exponent == 0 ? 4U : 32U;
  auto const tag = (exponent == 0 ? 0U : exponent * 4 + 2) + negative;
  if (magnitude > (TimeCode::kUpper - tag) / factor) {
    return TimeCode{kAbsolute};
  }
  return TimeCode{magnitude * factor + tag};
}

template <seri::BinaryBuffer B, typename T>
seri::SeriResult SaveTime(seri::BinaryArchive<B>& archive, T anchor, T value) {
  auto const code = EncodeTime(anchor, value);
  TRY_RESULT(archive.Save(code));
  if (code == kAbsolute) {
    return archive.Save(value);
  }
  return Ok{seri::good};
}

template <seri::BinaryBuffer B, typename T>
seri::SeriResult LoadTime(seri::BinaryArchive<B>& archive, T anchor, T& value) {
  TimeCode wire;
  TRY_RESULT(archive.Load(wire));
  auto const code = static_cast<TimeCode::ValueType>(wire);
  if (code == kAbsolute) {
    return archive.Load(value);
  }
  std::uint64_t magnitude = code >> 2;
  if ((code & 2U) != 0) {
    auto const exponent = magnitude & 7U;
    magnitude >>= 3;
    if (magnitude == 0 || exponent == 0 || exponent > 6) {
      return Error{seri::read_error};
    }
    magnitude *= kScale[exponent];
  }
  if ((code & 1U) != 0) {
    if (magnitude == 0 || magnitude > anchor) {
      return Error{seri::read_error};
    }
    value = static_cast<T>(anchor - magnitude);
  } else {
    if (magnitude > std::numeric_limits<T>::max() - anchor) {
      return Error{seri::read_error};
    }
    value = static_cast<T>(anchor + magnitude);
  }
  return Ok{seri::good};
}
}  // namespace adaptive_datagram_time_internal
}  // namespace ae::adaptive_datagram

namespace ae::seri {
template <BinaryBuffer B>
struct Serializer<BinaryArchive<B>, adaptive_datagram::DatagramTiming> {
  using Times = adaptive_datagram::DatagramTiming;
  SeriResult Seri(BinaryArchive<B>& archive, Meta<Times const> meta) const {
    using adaptive_datagram::adaptive_datagram_time_internal::SaveTime;
    auto const& t = meta.value;
    TRY_RESULT(archive.Save(t.queued_at_us));
    TRY_RESULT(SaveTime(archive, t.queued_at_us, t.sent_at_us));
    return SaveTime(archive, std::uint32_t{0}, t.lifetime_us);
  }
  SeriResult Deseri(BinaryArchive<B>& archive, Meta<Times> meta) const {
    using adaptive_datagram::adaptive_datagram_time_internal::LoadTime;
    auto& t = meta.value;
    TRY_RESULT(archive.Load(t.queued_at_us));
    TRY_RESULT(LoadTime(archive, t.queued_at_us, t.sent_at_us));
    return LoadTime(archive, std::uint32_t{0}, t.lifetime_us);
  }
};

template <BinaryBuffer B>
struct Serializer<BinaryArchive<B>, adaptive_datagram::FeedbackTiming> {
  using Times = adaptive_datagram::FeedbackTiming;
  SeriResult Seri(BinaryArchive<B>& archive, Meta<Times const> meta) const {
    using adaptive_datagram::adaptive_datagram_time_internal::SaveTime;
    auto const& t = meta.value;
    TRY_RESULT(archive.Save(t.echo_sent_at_us));
    TRY_RESULT(SaveTime(archive, t.echo_sent_at_us, t.received_at_us));
    return SaveTime(archive, t.received_at_us, t.reported_at_us);
  }
  SeriResult Deseri(BinaryArchive<B>& archive, Meta<Times> meta) const {
    using adaptive_datagram::adaptive_datagram_time_internal::LoadTime;
    auto& t = meta.value;
    TRY_RESULT(archive.Load(t.echo_sent_at_us));
    TRY_RESULT(LoadTime(archive, t.echo_sent_at_us, t.received_at_us));
    return LoadTime(archive, t.received_at_us, t.reported_at_us);
  }
};
}  // namespace ae::seri

#endif  // AETHER_ADAPTIVE_DATAGRAM_ADAPTIVE_DATAGRAM_TIME_H_
