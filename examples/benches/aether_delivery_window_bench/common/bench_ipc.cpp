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

#include "bench_ipc.h"

#include <algorithm>
#include <cstring>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace ae::bench::dw {

std::uint32_t IpcFrameCrc(IpcFrame const& frame) noexcept {
  IpcFrame tmp = frame;
  tmp.crc = 0;
  return Crc32(&tmp, sizeof(tmp));
}

void EncodeIpcFrame(IpcFrame& frame) noexcept {
  frame.magic = kIpcMagic;
  frame.version = kIpcVersion;
  frame.crc = IpcFrameCrc(frame);
}

bool DecodeIpcFrame(void const* data, std::size_t size,
                    IpcFrame& out) noexcept {
  if (data == nullptr || size < sizeof(IpcFrame)) {
    return false;
  }
  std::memcpy(&out, data, sizeof(IpcFrame));
  if (out.magic != kIpcMagic || out.version != kIpcVersion) {
    return false;
  }
  return IpcFrameCrc(out) == out.crc;
}

std::string PipeNameFor(std::string const& run_id, Side side) {
  return PipeNameFor(run_id, side, "");
}

std::string PipeNameFor(std::string const& run_id, Side side,
                        std::string const& suffix) {
  auto const s = side == Side::kA ? "a" : (side == Side::kB ? "b" : "c");
  auto name = "\\\\.\\pipe\\aether-dw-bench-" + run_id + "-" + s;
  if (!suffix.empty()) {
    name += "-";
    name += suffix;
  }
  return name;
}

std::uint32_t HashRunId(std::string const& run_id) {
  std::uint32_t h = 2166136261u;
  for (char c : run_id) {
    h ^= static_cast<std::uint8_t>(c);
    h *= 16777619u;
  }
  return h;
}

#if defined(_WIN32)

namespace {

bool WriteAllOv(HANDLE h, void const* data, DWORD size) {
  auto const* p = static_cast<char const*>(data);
  DWORD written_total = 0;
  while (written_total < size) {
    OVERLAPPED ov{};
    ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (ov.hEvent == nullptr) {
      return false;
    }
    DWORD written = 0;
    auto ok = WriteFile(h, p + written_total, size - written_total, &written,
                        &ov);
    if (!ok) {
      auto const err = GetLastError();
      if (err != ERROR_IO_PENDING) {
        CloseHandle(ov.hEvent);
        return false;
      }
      if (WaitForSingleObject(ov.hEvent, 30000) != WAIT_OBJECT_0) {
        CancelIoEx(h, &ov);
        CloseHandle(ov.hEvent);
        return false;
      }
      if (!GetOverlappedResult(h, &ov, &written, FALSE)) {
        CloseHandle(ov.hEvent);
        return false;
      }
    }
    CloseHandle(ov.hEvent);
    if (written == 0) {
      return false;
    }
    written_total += written;
  }
  return true;
}

bool ReadExactOv(HANDLE h, void* data, DWORD size, DWORD timeout_ms) {
  auto const deadline = GetTickCount64() + timeout_ms;
  auto* p = static_cast<char*>(data);
  DWORD got = 0;
  while (got < size) {
    auto const now = GetTickCount64();
    if (now >= deadline) {
      return false;
    }
    auto const remain = static_cast<DWORD>(deadline - now);
    OVERLAPPED ov{};
    ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    if (ov.hEvent == nullptr) {
      return false;
    }
    DWORD n = 0;
    auto ok = ReadFile(h, p + got, size - got, &n, &ov);
    if (!ok) {
      auto const err = GetLastError();
      if (err != ERROR_IO_PENDING) {
        CloseHandle(ov.hEvent);
        return false;
      }
      auto const wait = WaitForSingleObject(ov.hEvent, remain);
      if (wait != WAIT_OBJECT_0) {
        CancelIoEx(h, &ov);
        CloseHandle(ov.hEvent);
        return false;
      }
      if (!GetOverlappedResult(h, &ov, &n, FALSE)) {
        CloseHandle(ov.hEvent);
        return false;
      }
    }
    CloseHandle(ov.hEvent);
    if (n == 0) {
      return false;
    }
    got += n;
  }
  return true;
}

bool WriteAll(HANDLE h, void const* data, DWORD size) {
  auto const* p = static_cast<char const*>(data);
  DWORD written_total = 0;
  while (written_total < size) {
    DWORD written = 0;
    if (!WriteFile(h, p + written_total, size - written_total, &written,
                   nullptr)) {
      return false;
    }
    if (written == 0) {
      return false;
    }
    written_total += written;
  }
  return true;
}

bool ReadExact(HANDLE h, void* data, DWORD size, DWORD timeout_ms) {
  auto const deadline = GetTickCount64() + timeout_ms;
  auto* p = static_cast<char*>(data);
  DWORD got = 0;
  while (got < size) {
    auto const now = GetTickCount64();
    if (now >= deadline) {
      return false;
    }
    DWORD avail = 0;
    if (!PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr)) {
      return false;
    }
    if (avail == 0) {
      Sleep(1);
      continue;
    }
    auto const chunk =
        (std::min)(avail, static_cast<DWORD>(size - got));
    DWORD n = 0;
    if (!ReadFile(h, p + got, chunk, &n, nullptr)) {
      return false;
    }
    if (n == 0) {
      return false;
    }
    got += n;
  }
  return true;
}

}  // namespace

NamedPipeServer::~NamedPipeServer() { Close(); }

bool NamedPipeServer::Create(std::string const& pipe_name) {
  Close();
  handle_ = CreateNamedPipeA(
      pipe_name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
      1, 4096, 4096, 0, nullptr);
  return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
}

bool NamedPipeServer::WaitForClient(std::uint32_t timeout_ms) {
  if (handle_ == nullptr || handle_ == INVALID_HANDLE_VALUE) {
    return false;
  }
  OVERLAPPED ov{};
  ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  if (ov.hEvent == nullptr) {
    return false;
  }
  auto connected = ConnectNamedPipe(static_cast<HANDLE>(handle_), &ov);
  if (!connected) {
    auto const err = GetLastError();
    if (err == ERROR_PIPE_CONNECTED) {
      CloseHandle(ov.hEvent);
      return true;
    }
    if (err != ERROR_IO_PENDING) {
      CloseHandle(ov.hEvent);
      return false;
    }
    auto const wait = WaitForSingleObject(ov.hEvent, timeout_ms);
    if (wait != WAIT_OBJECT_0) {
      CancelIoEx(static_cast<HANDLE>(handle_), &ov);
      CloseHandle(ov.hEvent);
      return false;
    }
    DWORD transferred = 0;
    auto const ok =
        GetOverlappedResult(static_cast<HANDLE>(handle_), &ov, &transferred,
                            FALSE);
    CloseHandle(ov.hEvent);
    return ok == TRUE || GetLastError() == ERROR_PIPE_CONNECTED;
  }
  CloseHandle(ov.hEvent);
  return true;
}

bool NamedPipeServer::WriteFrame(IpcFrame frame) {
  EncodeIpcFrame(frame);
  return WriteAllOv(static_cast<HANDLE>(handle_), &frame,
                    static_cast<DWORD>(sizeof(frame)));
}

std::optional<IpcFrame> NamedPipeServer::TryReadFrame(
    std::uint32_t timeout_ms) {
  IpcFrame frame{};
  if (!ReadExactOv(static_cast<HANDLE>(handle_), &frame,
                   static_cast<DWORD>(sizeof(frame)), timeout_ms)) {
    return std::nullopt;
  }
  if (!DecodeIpcFrame(&frame, sizeof(frame), frame)) {
    return std::nullopt;
  }
  return frame;
}

void NamedPipeServer::Close() {
  if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(static_cast<HANDLE>(handle_));
  }
  handle_ = nullptr;
}

NamedPipeClient::~NamedPipeClient() { Close(); }

bool NamedPipeClient::Connect(std::string const& pipe_name,
                              std::uint32_t timeout_ms) {
  Close();
  auto const start = GetTickCount64();
  for (;;) {
    handle_ = CreateFileA(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                          nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle_ != INVALID_HANDLE_VALUE) {
      DWORD mode = PIPE_READMODE_BYTE;
      SetNamedPipeHandleState(static_cast<HANDLE>(handle_), &mode, nullptr,
                              nullptr);
      return true;
    }
    if (GetTickCount64() - start >= timeout_ms) {
      handle_ = nullptr;
      return false;
    }
    WaitNamedPipeA(pipe_name.c_str(), 200);
  }
}

bool NamedPipeClient::WriteFrame(IpcFrame frame) {
  EncodeIpcFrame(frame);
  return WriteAll(static_cast<HANDLE>(handle_), &frame,
                  static_cast<DWORD>(sizeof(frame)));
}

std::optional<IpcFrame> NamedPipeClient::TryReadFrame(
    std::uint32_t timeout_ms) {
  IpcFrame frame{};
  if (!ReadExact(static_cast<HANDLE>(handle_), &frame,
                 static_cast<DWORD>(sizeof(frame)), timeout_ms)) {
    return std::nullopt;
  }
  if (!DecodeIpcFrame(&frame, sizeof(frame), frame)) {
    return std::nullopt;
  }
  return frame;
}

void NamedPipeClient::Close() {
  if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(static_cast<HANDLE>(handle_));
  }
  handle_ = nullptr;
}

bool WaitMsPrecise(std::int64_t delay_ms) {
  if (delay_ms <= 0) {
    return true;
  }
  HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr,
                                        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                        TIMER_ALL_ACCESS);
  if (timer == nullptr) {
    timer = CreateWaitableTimerW(nullptr, TRUE, nullptr);
  }
  if (timer == nullptr) {
    Sleep(static_cast<DWORD>(delay_ms));
    return true;
  }
  LARGE_INTEGER due{};
  due.QuadPart = -delay_ms * 10000LL;  // 100ns units, negative = relative
  if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
    CloseHandle(timer);
    Sleep(static_cast<DWORD>(delay_ms));
    return true;
  }
  WaitForSingleObject(timer, INFINITE);
  CloseHandle(timer);
  return true;
}

#endif  // _WIN32

}  // namespace ae::bench::dw
