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

#include "deadline_ipc.h"

#include <cstring>

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace ae::test_uap_peer_deadline {

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
  auto const s = side == Side::kA ? "a" : (side == Side::kB ? "b" : "c");
  return "\\\\.\\pipe\\aether-uap-deadline-" + run_id + "-" + s;
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

bool OverlappedWait(HANDLE handle, OVERLAPPED& ov, DWORD timeout_ms,
                    DWORD* transferred) {
  DWORD bytes = 0;
  if (GetOverlappedResult(handle, &ov, &bytes, FALSE)) {
    if (transferred != nullptr) {
      *transferred = bytes;
    }
    return true;
  }
  if (GetLastError() != ERROR_IO_INCOMPLETE) {
    return false;
  }
  auto const wait = WaitForSingleObject(ov.hEvent, timeout_ms);
  if (wait != WAIT_OBJECT_0) {
    CancelIoEx(handle, &ov);
    return false;
  }
  if (!GetOverlappedResult(handle, &ov, &bytes, FALSE)) {
    return false;
  }
  if (transferred != nullptr) {
    *transferred = bytes;
  }
  return true;
}

}  // namespace

NamedPipeServer::~NamedPipeServer() { Close(); }

bool NamedPipeServer::Create(std::string const& pipe_name) {
  Close();
  handle_ = CreateNamedPipeA(
      pipe_name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT, 1, 4096, 4096, 0,
      nullptr);
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
  if (connected) {
    CloseHandle(ov.hEvent);
    return true;
  }
  auto const err = GetLastError();
  if (err == ERROR_PIPE_CONNECTED) {
    CloseHandle(ov.hEvent);
    return true;
  }
  if (err != ERROR_IO_PENDING) {
    CloseHandle(ov.hEvent);
    return false;
  }
  auto const ok = OverlappedWait(static_cast<HANDLE>(handle_), ov, timeout_ms,
                                 nullptr);
  CloseHandle(ov.hEvent);
  return ok;
}

bool NamedPipeServer::WriteFrame(IpcFrame frame) {
  EncodeIpcFrame(frame);
  OVERLAPPED ov{};
  ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  if (ov.hEvent == nullptr) {
    return false;
  }
  DWORD written = 0;
  auto ok = WriteFile(static_cast<HANDLE>(handle_), &frame, sizeof(frame),
                      &written, &ov);
  if (!ok) {
    if (GetLastError() != ERROR_IO_PENDING) {
      CloseHandle(ov.hEvent);
      return false;
    }
    ok = OverlappedWait(static_cast<HANDLE>(handle_), ov, 5000, &written);
  }
  CloseHandle(ov.hEvent);
  return ok && written == sizeof(frame);
}

std::optional<IpcFrame> NamedPipeServer::TryReadFrame(
    std::uint32_t timeout_ms) {
  IpcFrame frame{};
  OVERLAPPED ov{};
  ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
  if (ov.hEvent == nullptr) {
    return std::nullopt;
  }
  DWORD read = 0;
  auto ok = ReadFile(static_cast<HANDLE>(handle_), &frame, sizeof(frame), &read,
                     &ov);
  if (!ok) {
    if (GetLastError() != ERROR_IO_PENDING) {
      CloseHandle(ov.hEvent);
      return std::nullopt;
    }
    ok = OverlappedWait(static_cast<HANDLE>(handle_), ov, timeout_ms, &read);
  }
  CloseHandle(ov.hEvent);
  if (!ok || read < sizeof(IpcFrame)) {
    return std::nullopt;
  }
  IpcFrame out{};
  if (!DecodeIpcFrame(&frame, sizeof(frame), out)) {
    return std::nullopt;
  }
  return out;
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
  auto const deadline = GetTickCount64() + timeout_ms;
  while (GetTickCount64() < deadline) {
    handle_ = CreateFileA(pipe_name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
                          nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle_ != INVALID_HANDLE_VALUE) {
      DWORD mode = PIPE_READMODE_MESSAGE;
      SetNamedPipeHandleState(static_cast<HANDLE>(handle_), &mode, nullptr,
                              nullptr);
      return true;
    }
    if (GetLastError() != ERROR_PIPE_BUSY) {
      Sleep(50);
      continue;
    }
    WaitNamedPipeA(pipe_name.c_str(), 200);
  }
  handle_ = nullptr;
  return false;
}

bool NamedPipeClient::WriteFrame(IpcFrame frame) {
  EncodeIpcFrame(frame);
  DWORD written = 0;
  return WriteFile(static_cast<HANDLE>(handle_), &frame, sizeof(frame),
                   &written, nullptr) &&
         written == sizeof(frame);
}

std::optional<IpcFrame> NamedPipeClient::TryReadFrame(
    std::uint32_t timeout_ms) {
  auto const deadline = GetTickCount64() + timeout_ms;
  while (GetTickCount64() <= deadline) {
    DWORD avail = 0;
    if (!PeekNamedPipe(static_cast<HANDLE>(handle_), nullptr, 0, nullptr,
                       &avail, nullptr)) {
      return std::nullopt;
    }
    if (avail < sizeof(IpcFrame)) {
      Sleep(5);
      continue;
    }
    IpcFrame frame{};
    DWORD read = 0;
    if (!ReadFile(static_cast<HANDLE>(handle_), &frame, sizeof(frame), &read,
                  nullptr) ||
        read < sizeof(IpcFrame)) {
      return std::nullopt;
    }
    IpcFrame out{};
    if (!DecodeIpcFrame(&frame, sizeof(frame), out)) {
      return std::nullopt;
    }
    return out;
  }
  return std::nullopt;
}

void NamedPipeClient::Close() {
  if (handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(static_cast<HANDLE>(handle_));
  }
  handle_ = nullptr;
}

#endif

}  // namespace ae::test_uap_peer_deadline
