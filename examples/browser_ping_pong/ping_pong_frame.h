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

#ifndef EXAMPLES_BROWSER_PING_PONG_PING_PONG_FRAME_H_
#define EXAMPLES_BROWSER_PING_PONG_PING_PONG_FRAME_H_

#include <cstdint>
#include <cstring>
#include <optional>
#include <vector>

#include "aether/types/data_buffer.h"

namespace ae::examples::browser_ping_pong {

// Application-level PING/PONG frame v1 (little-endian). See
// docs/emscripten_browser_transport.md §Application-level PING/PONG frame.
inline constexpr char kFrameMagic[4] = {'A', 'E', 'P', 'P'};
inline constexpr std::uint8_t kFrameVersion = 1;
inline constexpr std::size_t kFrameHeaderSize = 28;
inline constexpr std::size_t kMaxFrameSize = 2048;

enum class FrameType : std::uint8_t {
  kPing = 1,
  kPong = 2,
};

struct PingPongFrame {
  std::uint8_t version{kFrameVersion};
  FrameType type{FrameType::kPing};
  std::uint32_t session{0};
  std::uint64_t sequence{0};
  std::uint64_t timestamp_mono_ns{0};
  std::vector<std::uint8_t> payload;
};

namespace ping_pong_frame_internal {

inline void WriteLe16(std::uint8_t* out, std::uint16_t value) {
  out[0] = static_cast<std::uint8_t>(value & 0xffu);
  out[1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
}

inline void WriteLe32(std::uint8_t* out, std::uint32_t value) {
  out[0] = static_cast<std::uint8_t>(value & 0xffu);
  out[1] = static_cast<std::uint8_t>((value >> 8) & 0xffu);
  out[2] = static_cast<std::uint8_t>((value >> 16) & 0xffu);
  out[3] = static_cast<std::uint8_t>((value >> 24) & 0xffu);
}

inline void WriteLe64(std::uint8_t* out, std::uint64_t value) {
  for (int i = 0; i < 8; ++i) {
    out[i] = static_cast<std::uint8_t>((value >> (8 * i)) & 0xffu);
  }
}

inline std::uint16_t ReadLe16(std::uint8_t const* in) {
  return static_cast<std::uint16_t>(in[0]) |
         (static_cast<std::uint16_t>(in[1]) << 8);
}

inline std::uint32_t ReadLe32(std::uint8_t const* in) {
  return static_cast<std::uint32_t>(in[0]) |
         (static_cast<std::uint32_t>(in[1]) << 8) |
         (static_cast<std::uint32_t>(in[2]) << 16) |
         (static_cast<std::uint32_t>(in[3]) << 24);
}

inline std::uint64_t ReadLe64(std::uint8_t const* in) {
  std::uint64_t value = 0;
  for (int i = 0; i < 8; ++i) {
    value |= static_cast<std::uint64_t>(in[i]) << (8 * i);
  }
  return value;
}

}  // namespace ping_pong_frame_internal

inline std::optional<DataBuffer> EncodeFrame(PingPongFrame const& frame) {
  if (frame.payload.size() >
      (kMaxFrameSize > kFrameHeaderSize ? kMaxFrameSize - kFrameHeaderSize
                                        : 0)) {
    return std::nullopt;
  }
  DataBuffer out(kFrameHeaderSize + frame.payload.size());
  std::memcpy(out.data(), kFrameMagic, 4);
  out[4] = frame.version;
  out[5] = static_cast<std::uint8_t>(frame.type);
  ping_pong_frame_internal::WriteLe32(out.data() + 6, frame.session);
  ping_pong_frame_internal::WriteLe64(out.data() + 10, frame.sequence);
  ping_pong_frame_internal::WriteLe64(out.data() + 18, frame.timestamp_mono_ns);
  ping_pong_frame_internal::WriteLe16(
      out.data() + 26, static_cast<std::uint16_t>(frame.payload.size()));
  if (!frame.payload.empty()) {
    std::memcpy(out.data() + kFrameHeaderSize, frame.payload.data(),
                frame.payload.size());
  }
  return out;
}

inline std::optional<PingPongFrame> DecodeFrame(DataBuffer const& data) {
  if (data.size() < kFrameHeaderSize || data.size() > kMaxFrameSize) {
    return std::nullopt;
  }
  if (std::memcmp(data.data(), kFrameMagic, 4) != 0) {
    return std::nullopt;
  }
  PingPongFrame frame;
  frame.version = data[4];
  if (frame.version != kFrameVersion) {
    return std::nullopt;
  }
  auto const type = data[5];
  if (type != static_cast<std::uint8_t>(FrameType::kPing) &&
      type != static_cast<std::uint8_t>(FrameType::kPong)) {
    return std::nullopt;
  }
  frame.type = static_cast<FrameType>(type);
  frame.session = ping_pong_frame_internal::ReadLe32(data.data() + 6);
  frame.sequence = ping_pong_frame_internal::ReadLe64(data.data() + 10);
  frame.timestamp_mono_ns =
      ping_pong_frame_internal::ReadLe64(data.data() + 18);
  auto const payload_len =
      ping_pong_frame_internal::ReadLe16(data.data() + 26);
  if (data.size() != kFrameHeaderSize + payload_len) {
    return std::nullopt;
  }
  frame.payload.assign(data.begin() + static_cast<std::ptrdiff_t>(kFrameHeaderSize),
                       data.end());
  return frame;
}

}  // namespace ae::examples::browser_ping_pong

#endif  // EXAMPLES_BROWSER_PING_PONG_PING_PONG_FRAME_H_
