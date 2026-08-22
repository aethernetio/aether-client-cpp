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

#ifndef AETHER_DELIVERY_WINDOW_BENCH_COMMON_BENCH_IPC_H_
#define AETHER_DELIVERY_WINDOW_BENCH_COMMON_BENCH_IPC_H_

#include <cstdint>
#include <optional>
#include <string>

#include "bench_message.h"
#include "bench_types.h"

namespace ae::bench::dw {

#pragma pack(push, 1)
struct IpcFrame {
  std::uint32_t magic{kIpcMagic};
  std::uint8_t version{kIpcVersion};
  std::uint8_t type{0};
  std::uint8_t side{0};
  std::uint8_t event_kind{0};
  std::uint32_t run_id_hash{0};
  std::uint32_t seq{0};
  std::uint32_t config_id{0};
  std::uint32_t cycle_id{0};
  std::uint32_t message_id{0};
  std::uint16_t server_id{0};
  std::uint8_t direction{0};
  std::uint8_t reserved{0};
  std::int64_t local_steady_us{0};
  std::int64_t a{0};
  std::int64_t b{0};
  std::int64_t c{0};
  std::uint32_t crc{0};
};
#pragma pack(pop)

static_assert(sizeof(IpcFrame) < 128);

std::uint32_t IpcFrameCrc(IpcFrame const& frame) noexcept;
void EncodeIpcFrame(IpcFrame& frame) noexcept;
bool DecodeIpcFrame(void const* data, std::size_t size, IpcFrame& out) noexcept;

std::string PipeNameFor(std::string const& run_id, Side side);
std::string PipeNameFor(std::string const& run_id, Side side,
                        std::string const& suffix);
std::uint32_t HashRunId(std::string const& run_id);

#if defined(_WIN32)
class NamedPipeServer {
 public:
  NamedPipeServer() = default;
  ~NamedPipeServer();
  NamedPipeServer(NamedPipeServer const&) = delete;
  NamedPipeServer& operator=(NamedPipeServer const&) = delete;

  bool Create(std::string const& pipe_name);
  bool WaitForClient(std::uint32_t timeout_ms);
  bool WriteFrame(IpcFrame frame);
  std::optional<IpcFrame> TryReadFrame(std::uint32_t timeout_ms);
  void Close();

 private:
  void* handle_{nullptr};  // HANDLE
};

class NamedPipeClient {
 public:
  NamedPipeClient() = default;
  ~NamedPipeClient();
  NamedPipeClient(NamedPipeClient const&) = delete;
  NamedPipeClient& operator=(NamedPipeClient const&) = delete;

  bool Connect(std::string const& pipe_name, std::uint32_t timeout_ms);
  bool WriteFrame(IpcFrame frame);
  std::optional<IpcFrame> TryReadFrame(std::uint32_t timeout_ms);
  void Close();

 private:
  void* handle_{nullptr};
};

bool WaitMsPrecise(std::int64_t delay_ms);
#endif

}  // namespace ae::bench::dw

#endif  // AETHER_DELIVERY_WINDOW_BENCH_COMMON_BENCH_IPC_H_
