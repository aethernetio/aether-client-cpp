// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include "aether/telemetry_delivery.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <random>
#include <stdexcept>
#include "aether-miscpp/serialization/binary_archive.h"
#include "aether-tele/raw/types.h"
#include "aether/client_messages/p2p_message_stream.h"
namespace ae {
namespace telemetry_delivery_internal {
constexpr std::string_view kMagic = "AETLOG01";
constexpr std::size_t kMaxManifest = 1024 * 1024;
bool Nonzero(TelemetryBatchId const& id) {
  return std::any_of(id.begin(), id.end(), [](auto v) { return v != 0; });
}
}  // namespace telemetry_delivery_internal
bool ValidTelemetryProduct(std::string_view product) {
  return product == "apptraverse-app" || product == "aeroadmin-chat";
}
bool ValidTelemetryArtifact(std::span<std::uint8_t const> artifact) {
  if (artifact.size() < 14 || artifact.size() > kTelemetryMaxArtifactBytes ||
      std::memcmp(artifact.data(), telemetry_delivery_internal::kMagic.data(),
                  8) != 0) {
    return false;
  }
  auto const length = static_cast<std::size_t>(artifact[8]) |
                      (static_cast<std::size_t>(artifact[9]) << 8) |
                      (static_cast<std::size_t>(artifact[10]) << 16) |
                      (static_cast<std::size_t>(artifact[11]) << 24);
  if (length < 2 || length > telemetry_delivery_internal::kMaxManifest ||
      length > artifact.size() - 12) {
    return false;
  }
  auto manifest = artifact.subspan(12, length);
  return manifest.front() == '{' && manifest.back() == '}' &&
         tele::raw::ValidUtf8(manifest);
}
std::vector<std::uint8_t> EncodeTelemetryArtifact(
    std::string_view manifest, std::span<std::uint8_t const> payload) {
  if (manifest.size() < 2 ||
      manifest.size() > telemetry_delivery_internal::kMaxManifest ||
      payload.size() > kTelemetryMaxArtifactBytes - 12 - manifest.size() ||
      manifest.front() != '{' || manifest.back() != '}' ||
      !tele::raw::ValidUtf8(manifest)) {
    return {};
  }
  std::vector<std::uint8_t> result(12);
  std::copy(telemetry_delivery_internal::kMagic.begin(),
            telemetry_delivery_internal::kMagic.end(), result.begin());
  tele::raw::Store32(result.data() + 8,
                     static_cast<std::uint32_t>(manifest.size()));
  result.insert(result.end(), manifest.begin(), manifest.end());
  result.insert(result.end(), payload.begin(), payload.end());
  return result;
}
std::string TelemetrySha256(std::span<std::uint8_t const> bytes) {
  constexpr std::array<std::uint32_t, 64> constants{
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
      0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
      0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
      0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
      0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
      0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
      0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
      0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
      0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  std::array<std::uint32_t, 8> hash{0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                    0xa54ff53a, 0x510e527f, 0x9b05688c,
                                    0x1f83d9ab, 0x5be0cd19};
  auto rotate = [](std::uint32_t n, unsigned r) {
    return (n >> r) | (n << (32 - r));
  };
  auto blocks = bytes.size() / 64 + 1 + (bytes.size() % 64 >= 56 ? 1 : 0);
  auto bits = static_cast<std::uint64_t>(bytes.size()) * 8;
  for (std::size_t block = 0; block < blocks; ++block) {
    std::array<std::uint8_t, 64> input{};
    for (std::size_t j = 0; j < 64; ++j) {
      auto at = block * 64 + j;
      if (at < bytes.size()) {
        input[j] = bytes[at];
      } else if (at == bytes.size()) {
        input[j] = 0x80;
      }
    }
    if (block + 1 == blocks) {
      for (unsigned j = 0; j < 8; ++j) {
        input[63 - j] = static_cast<std::uint8_t>(bits >> (j * 8));
      }
    }
    std::array<std::uint32_t, 64> words{};
    for (unsigned j = 0; j < 16; ++j) {
      words[j] = (std::uint32_t{input[j * 4]} << 24) |
                 (std::uint32_t{input[j * 4 + 1]} << 16) |
                 (std::uint32_t{input[j * 4 + 2]} << 8) | input[j * 4 + 3];
    }
    for (unsigned j = 16; j < 64; ++j) {
      auto a = words[j - 15], b = words[j - 2];
      words[j] = words[j - 16] + (rotate(a, 7) ^ rotate(a, 18) ^ (a >> 3)) +
                 words[j - 7] + (rotate(b, 17) ^ rotate(b, 19) ^ (b >> 10));
    }
    auto a = hash[0], b = hash[1], c = hash[2], d = hash[3], e = hash[4],
         f = hash[5], g = hash[6], h = hash[7];
    for (unsigned j = 0; j < 64; ++j) {
      auto t1 = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) +
                ((e & f) ^ (~e & g)) + constants[j] + words[j];
      auto t2 = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) +
                ((a & b) ^ (a & c) ^ (b & c));
      h = g;
      g = f;
      f = e;
      e = d + t1;
      d = c;
      c = b;
      b = a;
      a = t1 + t2;
    }
    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
  }
  constexpr char digits[] = "0123456789abcdef";
  std::string result;
  for (auto value : hash) {
    for (int shift = 28; shift >= 0; shift -= 4) {
      result += digits[(value >> shift) & 15];
    }
  }
  return result;
}
TelemetryBatchId NewTelemetryBatchId() {
  std::random_device random;
  TelemetryBatchId id{};
  do {
    for (std::size_t i = 0; i < id.size(); i += 4) {
      tele::raw::Store32(id.data() + i, random());
    }
  } while (!telemetry_delivery_internal::Nonzero(id));
  return id;
}
std::string TelemetryBatchIdText(TelemetryBatchId const& id) {
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  result.reserve(32);
  for (auto value : id) {
    result += hex[value >> 4];
    result += hex[value & 15];
  }
  return result;
}
std::vector<std::uint8_t> EncodeTelemetryDeliveryFrame(
    TelemetryDeliveryFrame const& frame) {
  std::vector<std::uint8_t> bytes;
  auto archive = seri::BinaryArchive{seri::BinaryVectorBuffer<>{bytes}};
  if (!archive.Save(frame)) {
    return {};
  }
  return bytes;
}
std::optional<TelemetryDeliveryFrame> DecodeTelemetryDeliveryFrame(
    std::span<std::uint8_t const> bytes) {
  if (bytes.empty() || bytes.size() > kTelemetryMaxFrameBytes) {
    return {};
  }
  std::vector<std::uint8_t> input(bytes.begin(), bytes.end());
  auto archive = seri::BinaryArchive{seri::BinaryVectorBuffer<>{input}};
  TelemetryDeliveryFrame result;
  if (!archive.Load(result) ||
      archive.buffer().read_position() != input.size() || result.version != 1 ||
      result.kind > 1 || !ValidTelemetryProduct(result.product) ||
      !telemetry_delivery_internal::Nonzero(result.id)) {
    return {};
  }
  if (result.kind == 1) {
    if (result.offset || result.total || !result.bytes.empty()) {
      return {};
    }
  } else if (result.total < 14 || result.total > kTelemetryMaxArtifactBytes ||
             result.offset >= result.total || result.bytes.empty() ||
             result.bytes.size() > result.total - result.offset) {
    return {};
  }
  return result;
}
TelemetryDeliveryPump::TelemetryDeliveryPump(TelemetryOutbox& outbox,
                                             Persist persist)
    : outbox_{&outbox}, persist_{std::move(persist)} {}
std::optional<std::vector<std::uint8_t>> TelemetryDeliveryPump::Request(
    std::size_t maximum) {
  if (writing_ || outbox_->pending.empty() || maximum < 96) {
    return {};
  }
  auto const& batch = outbox_->pending.front();
  if (!ValidTelemetryProduct(batch.product) ||
      !ValidTelemetryArtifact(batch.artifact) ||
      !telemetry_delivery_internal::Nonzero(batch.id)) {
    return {};
  }
  if (active_ != batch.id) {
    active_ = batch.id;
    offset_ = 0;
    saved_ = false;
  }
  if (!saved_) {
    bool persisted = false;
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
    try {
#endif
      persisted = persist_ && persist_();
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
    } catch (...) {
      persisted = false;
    }
#endif
    if (!persisted) {
      return {};
    }
    saved_ = true;
  }
  if (outbox_->pending.empty() || outbox_->pending.front().id != active_) {
    return {};
  }
  auto const& current = outbox_->pending.front();
  if (offset_ == current.artifact.size()) {
    return {};
  }
  maximum = std::min(maximum, kTelemetryMaxFrameBytes);
  TelemetryDeliveryFrame frame;
  frame.id = current.id;
  frame.product = current.product;
  frame.offset = offset_;
  frame.total = current.artifact.size();
  auto count = std::min(current.artifact.size() - offset_, maximum - 96);
  while (count) {
    frame.bytes.assign(current.artifact.begin() + offset_,
                       current.artifact.begin() + offset_ + count);
    auto bytes = EncodeTelemetryDeliveryFrame(frame);
    if (bytes.size() <= maximum) {
      writing_ = true;
      writing_bytes_ = count;
      return bytes;
    }
    count /= 2;
  }
  return {};
}
void TelemetryDeliveryPump::Written(bool success) {
  if (!writing_) {
    return;
  }
  writing_ = false;
  if (success) {
    offset_ += writing_bytes_;
  } else {
    offset_ = 0;
  }
  writing_bytes_ = 0;
}
bool TelemetryDeliveryPump::Receive(std::span<std::uint8_t const> bytes) {
  auto frame = DecodeTelemetryDeliveryFrame(bytes);
  if (!frame || frame->kind != 1 || outbox_->pending.empty()) {
    return false;
  }
  auto const& batch = outbox_->pending.front();
  if (frame->id != active_ || frame->id != batch.id ||
      frame->product != batch.product) {
    return false;
  }
  auto retained = batch;
  outbox_->pending.erase(outbox_->pending.begin());
  bool persisted = false;
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
  try {
#endif
    persisted = persist_ && persist_();
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
  } catch (...) {
    persisted = false;
  }
#endif
  if (!persisted) {
    outbox_->pending.insert(outbox_->pending.begin(), std::move(retained));
    return false;
  }
  active_ = {};
  offset_ = 0;
  writing_bytes_ = 0;
  writing_ = false;
  saved_ = false;
  return true;
}
void TelemetryDeliveryPump::Retry() {
  writing_ = false;
  writing_bytes_ = 0;
  offset_ = 0;
}

struct TelemetryDeliverySession::Impl : std::enable_shared_from_this<Impl> {
  Impl(AeContext ctx, Ptr<Client> const& client, Uid recipient,
       TelemetryOutbox& outbox, TelemetryDeliveryPump::Persist persist,
       Duration timeout)
      : context{ctx}, pump{outbox, std::move(persist)}, retry{timeout} {
    StreamRoutePolicy route;
    route.lan = false;
    route.direct_ip = false;
    route.hole_punching = false;
    route.cloud = true;
    stream =
        std::make_unique<P2pStream>(context, client, recipient,
                                    client->message_stream_manager().CreatePort(
                                        recipient, kTelemetryDeliveryStream),
                                    RequestPolicy::MainServer{}, route);
  }
  void Start() {
    auto weak = weak_from_this();
    receive =
        stream->out_data_event().Subscribe([weak](DataBuffer const& bytes) {
          if (auto self = weak.lock()) {
            if (self->pump.Receive(bytes)) {
              self->writing.Reset();
              self->in_write = false;
              self->deadline = Now() + self->retry;
              self->Schedule(std::chrono::milliseconds{1});
            }
          }
        });
    update = stream->stream_update_event().Subscribe([weak] {
      if (auto self = weak.lock()) {
        self->Schedule(std::chrono::milliseconds{1});
      }
    });
    deadline = Now() + retry;
    Schedule(std::chrono::milliseconds{1});
  }
  void Schedule(Duration delay) {
    if (stopped || scheduled) {
      return;
    }
    scheduled = true;
    auto weak = weak_from_this();
    auto posted = context.scheduler().DelayedTask(
        task,
        [weak] {
          if (auto self = weak.lock()) {
            self->scheduled = false;
            self->Tick();
          }
        },
        delay);
    if (!posted) {
      scheduled = false;
      std::fputs("Telemetry delivery task allocation failed\n", stderr);
      assert(false && "Telemetry delivery task allocation failed");
    }
  }
  void Tick() {
    if (stopped) {
      return;
    }
    // A write is owned and timed out by the SDK transport. Never detach its
    // completion callback and start overlapping retries while it is pending.
    // This deadline measures idle progress / the final durable ACK wait.
    if (!in_write && Now() >= deadline) {
      writing.Reset();
      pump.Retry();
      deadline = Now() + retry;
    }
    auto info = stream->stream_info();
    if (!in_write && info.is_writable && Now() >= send_after) {
      auto bytes = pump.Request(info.max_element_size);
      if (bytes) {
        in_write = true;
        deadline = Now() + retry;
        auto& action = stream->Write(std::move(*bytes));
        auto weak = weak_from_this();
        writing =
            action.status_event().Subscribe([weak](WriteAction::Status status) {
              if (auto self = weak.lock()) {
                self->pump.Written(status == WriteAction::Status::kSuccess);
                self->in_write = false;
                self->writing.Reset();
                self->deadline = Now() + self->retry;
                self->send_after = status == WriteAction::Status::kSuccess
                                       ? Now()
                                       : self->deadline;
                self->Schedule(std::chrono::milliseconds{1});
              }
            });
      }
    }
    Schedule(std::chrono::milliseconds{100});
  }
  void Stop() {
    stopped = true;
    task.Reset();
    receive.Reset();
    update.Reset();
    writing.Reset();
    stream.reset();
  }
  AeContext context;
  TelemetryDeliveryPump pump;
  Duration retry;
  TimePoint deadline{};
  TimePoint send_after{};
  std::unique_ptr<P2pStream> stream;
  Subscription receive, update, writing;
  TaskSubscription task;
  bool stopped{}, scheduled{}, in_write{};
};
TelemetryDeliverySession::TelemetryDeliverySession(
    AeContext const& context, Ptr<Client> const& client, Uid recipient,
    TelemetryOutbox& outbox, TelemetryDeliveryPump::Persist persist,
    Duration retry) {
  if (!client || recipient.empty() || retry <= Duration::zero()) {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
    throw std::invalid_argument(
        "Telemetry delivery requires a client, recipient and retry interval");
#else
    std::fputs(
        "Telemetry delivery requires a client, recipient and retry "
        "interval\n",
        stderr);
    std::abort();
#endif
  }
  impl_ = std::make_shared<Impl>(context, client, recipient, outbox,
                                 std::move(persist), retry);
  impl_->Start();
}
TelemetryDeliverySession::~TelemetryDeliverySession() { Stop(); }
void TelemetryDeliverySession::Pump() {
  if (impl_) {
    impl_->Schedule(std::chrono::milliseconds{1});
  }
}
void TelemetryDeliverySession::Stop() {
  if (impl_) {
    impl_->Stop();
    impl_.reset();
  }
}
}  // namespace ae
