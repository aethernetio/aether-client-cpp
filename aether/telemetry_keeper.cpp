// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
#include "aether/telemetry_keeper.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>
#include "aether/client_messages/p2p_message_stream.h"
#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#  include <cerrno>
#endif
namespace ae {
namespace telemetry_keeper_internal {
std::string SourceText(Uid source) {
  return TelemetryBatchIdText(source.value);
}
bool Matches(std::filesystem::path const& path,
             std::span<std::uint8_t const> bytes) {
  std::error_code error;
  if (std::filesystem::file_size(path, error) != bytes.size() || error) {
    return false;
  }
  std::ifstream input(path, std::ios::binary);
  std::vector<std::uint8_t> stored(std::istreambuf_iterator<char>{input}, {});
  return !input.bad() && stored.size() == bytes.size() &&
         std::equal(stored.begin(), stored.end(), bytes.begin());
}
bool SyncStored(std::filesystem::path const& path) {
#if defined(_WIN32)
  auto file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }
  bool ok = FlushFileBuffers(file) != 0;
  return CloseHandle(file) != 0 && ok;
#else
  int file = open(path.c_str(), O_RDONLY);
  if (file < 0) {
    return false;
  }
  bool ok = fsync(file) == 0;
  ok = close(file) == 0 && ok;
  // Newly created product/source directories also have parent entries. Sync
  // every ancestor so a first-run ACK has the same durability as later ACKs.
  for (auto directory = std::filesystem::absolute(path).parent_path();;) {
    int handle = open(directory.c_str(), O_RDONLY | O_DIRECTORY);
    if (handle < 0) {
      return false;
    }
    ok = fsync(handle) == 0 && ok;
    ok = close(handle) == 0 && ok;
    auto parent = directory.parent_path();
    if (parent == directory) {
      break;
    }
    directory = std::move(parent);
  }
  return ok;
#endif
}
bool Commit(std::filesystem::path const& target,
            std::span<std::uint8_t const> bytes) {
  std::error_code error;
  std::filesystem::create_directories(target.parent_path(), error);
  if (error) {
    return false;
  }
  auto temporary = target;
  temporary += ".tmp." + TelemetryBatchIdText(NewTelemetryBatchId());
#if defined(_WIN32)
  auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
                          CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }
  bool ok = true;
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    DWORD written{};
    auto count = static_cast<DWORD>(
        std::min<std::size_t>(bytes.size() - offset, 1024 * 1024));
    if (!WriteFile(file, bytes.data() + offset, count, &written, nullptr) ||
        !written) {
      ok = false;
      break;
    }
    offset += written;
  }
  ok = FlushFileBuffers(file) != 0 && ok;
  ok = CloseHandle(file) != 0 && ok;
  if (ok) {
    ok = MoveFileExW(temporary.c_str(), target.c_str(),
                     MOVEFILE_WRITE_THROUGH) != 0;
  }
  if (!ok) {
    DeleteFileW(temporary.c_str());
  }
  return ok;
#else
  int file = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (file < 0) {
    return false;
  }
  bool ok = true;
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    auto count = write(file, bytes.data() + offset, bytes.size() - offset);
    if (count < 0 && errno == EINTR) {
      continue;
    }
    if (count <= 0) {
      ok = false;
      break;
    }
    offset += static_cast<std::size_t>(count);
  }
  ok = fsync(file) == 0 && ok;
  ok = close(file) == 0 && ok;
  if (ok) {
    ok = link(temporary.c_str(), target.c_str()) == 0;
  }
  unlink(temporary.c_str());
  return ok && SyncStored(target);
#endif
}
}  // namespace telemetry_keeper_internal
TelemetryKeeperStore::TelemetryKeeperStore(std::filesystem::path root)
    : root_{std::move(root)} {}
std::filesystem::path TelemetryKeeperStore::PathFor(
    Uid source, TelemetryBatch const& batch) const {
  return root_ / batch.product / telemetry_keeper_internal::SourceText(source) /
         (TelemetryBatchIdText(batch.id) + ".aetlog");
}
TelemetryStoreResult TelemetryKeeperStore::Store(Uid source,
                                                 TelemetryBatch const& batch) {
  if (source.empty() || !ValidTelemetryProduct(batch.product) ||
      !ValidTelemetryArtifact(batch.artifact) ||
      batch.id == TelemetryBatchId{}) {
    return TelemetryStoreResult::kRejected;
  }
  auto lock = std::scoped_lock{mutex_};
  auto path = PathFor(source, batch);
  std::error_code error;
  if (std::filesystem::exists(path, error)) {
    return telemetry_keeper_internal::Matches(path, batch.artifact) &&
                   telemetry_keeper_internal::SyncStored(path)
               ? TelemetryStoreResult::kDuplicate
               : TelemetryStoreResult::kRejected;
  }
  if (error) {
    return TelemetryStoreResult::kRejected;
  }
  if (telemetry_keeper_internal::Commit(path, batch.artifact)) {
    return TelemetryStoreResult::kStored;
  }
  // A second keeper process can win the create-without-replace race.
  return telemetry_keeper_internal::Matches(path, batch.artifact) &&
                 telemetry_keeper_internal::SyncStored(path)
             ? TelemetryStoreResult::kDuplicate
             : TelemetryStoreResult::kRejected;
}
struct TelemetryKeeperReceiver::Impl {
  explicit Impl(TelemetryKeeperStore& target) : store{&target} {}
  struct Pending {
    TelemetryBatch batch;
    std::vector<bool> received;
    std::size_t count{};
    std::chrono::steady_clock::time_point touched{};
  };
  std::optional<DataBuffer> Receive(Uid source,
                                    std::span<std::uint8_t const> bytes) {
    auto frame = DecodeTelemetryDeliveryFrame(bytes);
    if (!frame || frame->kind != 0 || source.empty()) {
      return {};
    }
    auto now = std::chrono::steady_clock::now();
    std::size_t held = 0;
    for (auto it = pending.begin(); it != pending.end();) {
      if (now - it->second.touched > std::chrono::minutes{2}) {
        it = pending.erase(it);
      } else {
        held += it->second.batch.artifact.size();
        ++it;
      }
    }
    auto key = std::pair{source, frame->id};
    auto found = pending.find(key);
    if (found == pending.end()) {
      if (pending.size() >= 8 || frame->total > 64 * 1024 * 1024 - held) {
        return {};
      }
      Pending value;
      value.batch = {
          frame->id, frame->product,
          std::vector<std::uint8_t>(static_cast<std::size_t>(frame->total))};
      value.received.resize(static_cast<std::size_t>(frame->total));
      found = pending.emplace(key, std::move(value)).first;
    }
    auto& value = found->second;
    value.touched = now;
    if (value.batch.product != frame->product ||
        value.batch.artifact.size() != frame->total) {
      pending.erase(found);
      return {};
    }
    for (std::size_t i = 0; i < frame->bytes.size(); ++i) {
      auto at = static_cast<std::size_t>(frame->offset) + i;
      if (value.received[at] && value.batch.artifact[at] != frame->bytes[i]) {
        pending.erase(found);
        return {};
      }
      if (!value.received[at]) {
        value.received[at] = true;
        ++value.count;
      }
      value.batch.artifact[at] = frame->bytes[i];
    }
    if (value.count != value.batch.artifact.size()) {
      return {};
    }
    auto result = store->Store(source, value.batch);
    pending.erase(found);
    if (result == TelemetryStoreResult::kRejected) {
      return {};
    }
    TelemetryDeliveryFrame ack;
    ack.kind = 1;
    ack.id = frame->id;
    ack.product = frame->product;
    return EncodeTelemetryDeliveryFrame(ack);
  }
  TelemetryKeeperStore* store;
  std::map<std::pair<Uid, TelemetryBatchId>, Pending> pending;
};
TelemetryKeeperReceiver::TelemetryKeeperReceiver(TelemetryKeeperStore& store)
    : impl_{std::make_unique<Impl>(store)} {}
TelemetryKeeperReceiver::~TelemetryKeeperReceiver() = default;
std::optional<DataBuffer> TelemetryKeeperReceiver::Receive(
    Uid source, std::span<std::uint8_t const> frame) {
  return impl_->Receive(source, frame);
}
struct TelemetryKeeperSession::Impl : std::enable_shared_from_this<Impl> {
  struct Connection {
    std::unique_ptr<P2pStream> stream;
    Subscription receive;
  };
  Impl(AeContext ctx, Ptr<Client> const& local, TelemetryKeeperStore& store)
      : context{ctx}, client{local}, receiver{store} {}
  void Start() {
    auto weak = weak_from_this();
    ports = client->message_stream_manager().new_port_event().Subscribe(
        [weak](P2pPortHandle& handle) {
          auto self = weak.lock();
          if (!self || handle.stream_id() != kTelemetryDeliveryStream) {
            return;
          }
          auto source = handle.destination();
          auto connection = std::make_unique<Connection>();
          StreamRoutePolicy route;
          route.lan = false;
          route.direct_ip = false;
          route.hole_punching = false;
          route.cloud = true;
          connection->stream = std::make_unique<P2pStream>(
              self->context, self->client, source, std::move(handle),
              RequestPolicy::MainServer{}, route);
          auto stream = connection->stream.get();
          connection->receive = stream->out_data_event().Subscribe(
              [weak, source, stream](DataBuffer const& bytes) {
                if (auto live = weak.lock()) {
                  if (auto ack = live->receiver.Receive(source, bytes)) {
                    stream->Write(std::move(*ack));
                  }
                }
              });
          self->connections.insert_or_assign(source, std::move(connection));
        });
  }
  AeContext context;
  Ptr<Client> client;
  TelemetryKeeperReceiver receiver;
  Subscription ports;
  std::map<Uid, std::unique_ptr<Connection>> connections;
};
TelemetryKeeperSession::TelemetryKeeperSession(AeContext const& context,
                                               Ptr<Client> const& client,
                                               TelemetryKeeperStore& store) {
  if (!client) {
    throw std::invalid_argument("Telemetry keeper requires a loaded client");
  }
  impl_ = std::make_shared<Impl>(context, client, store);
  impl_->Start();
}
TelemetryKeeperSession::~TelemetryKeeperSession() = default;
}  // namespace ae
