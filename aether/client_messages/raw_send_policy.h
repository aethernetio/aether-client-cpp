// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#ifndef AETHER_CLIENT_MESSAGES_RAW_SEND_POLICY_H_
#define AETHER_CLIENT_MESSAGES_RAW_SEND_POLICY_H_
#include <cstdint>
#include <optional>
#include <span>
#include "aether-miscpp/reflect/reflect.h"
#include "aether/types/data_buffer.h"
namespace ae {
enum class SendPriority : std::uint8_t {
  kInteractive = 0,
  kRealtime = 64,
  kNormal = 128,
  kBackground = 255
};
struct StreamSendPolicy {
  AE_REFLECT_MEMBERS(priority, max_packet_bytes)
  SendPriority priority{SendPriority::kNormal};
  // Application payload limit; reliable/adaptive wrappers fragment above this.
  std::uint32_t max_packet_bytes{1200};
};
// All rates are charged bytes/second. Times are monotonic microseconds.
struct RawResourceConfig {
  unsigned max_flows{32};
  unsigned slots_per_flow{2};
  unsigned bytes_per_flow{8192};
  unsigned flight_bytes{1024 * 1024};
  unsigned flight_packets{4096};
  unsigned initial_rate{250000}, min_rate{16000}, max_rate{8000000};
  unsigned target_extra_rtt_us{30000};
  unsigned receipt_timeout_us{1500000};
  unsigned queue_lifetime_us{200000};
};
struct RawReceipt {
  std::uint64_t epoch, sequence;
};
inline constexpr unsigned kRawHeaderBytes = 17;
DataBuffer RawFrame(RawReceipt tag, std::span<std::uint8_t const> payload);
DataBuffer RawAck(RawReceipt tag);
std::optional<RawReceipt> ReadRawHeader(std::span<std::uint8_t const> frame,
                                        bool ack);

}  // namespace ae
#endif
