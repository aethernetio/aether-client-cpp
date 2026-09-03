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

#ifndef AETHER_TRANSPORT_BROWSER_BROWSER_TRANSPORT_COMMON_H_
#define AETHER_TRANSPORT_BROWSER_BROWSER_TRANSPORT_COMMON_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "aether/ae_context.h"
#include "aether/common.h"
#include "aether/transport/packet_send_action.h"
#include "aether/types/data_buffer.h"
#include "aether/types/packed_size.h"
#include "aether/vector_buffer.h"
#include "aether/write_action/failed_write_action.h"
#include "aether/write_action/write_action.h"

namespace ae {
namespace browser_transport_internal {

#ifndef AE_BROWSER_PACKET_QUEUE_SIZE
#  define AE_BROWSER_PACKET_QUEUE_SIZE 100
#endif

#ifndef AE_BROWSER_WS_BUFFERED_AMOUNT_HIGH_WATER
#  define AE_BROWSER_WS_BUFFERED_AMOUNT_HIGH_WATER (256u * 1024u)
#endif

inline constexpr std::size_t kBrowserPacketQueueSize =
    AE_BROWSER_PACKET_QUEUE_SIZE;
inline constexpr std::size_t kBrowserWsBufferedAmountHighWater =
    AE_BROWSER_WS_BUFFERED_AMOUNT_HIGH_WATER;

/**
 * \brief Prefix a payload with the same PacketSize framing TcpTransport uses.
 */
inline DataBuffer FrameLengthPrefixedPacket(DataBuffer&& payload) {
  DataBuffer packet_data;
  VectorBuffer<PacketSize> vw{packet_data};
  vw.Write(seri::SizeWriteTag(payload.size()));
  vw.Write(seri::DataWriteTag{payload.data(), payload.size()});
  return packet_data;
}

/**
 * \brief Generation token so JS/network callbacks ignore destroyed or restreamed
 * transports.
 */
class GenerationGuard {
 public:
  GenerationGuard()
      : generation_{std::make_shared<std::atomic<std::uint64_t>>(0)} {}

  std::uint64_t current() const noexcept {
    return generation_->load(std::memory_order_acquire);
  }

  std::shared_ptr<std::atomic<std::uint64_t>> shared() const noexcept {
    return generation_;
  }

  std::uint64_t Bump() noexcept {
    return generation_->fetch_add(1, std::memory_order_acq_rel) + 1;
  }

  static bool IsCurrent(
      std::shared_ptr<std::atomic<std::uint64_t>> const& generation,
      std::uint64_t expected) noexcept {
    return generation &&
           generation->load(std::memory_order_acquire) == expected;
  }

 private:
  std::shared_ptr<std::atomic<std::uint64_t>> generation_;
};

/**
 * \brief Write action that completes only after an explicit Success/Fail path.
 * Used by browser transports so early-queued and in-flight writes stay alive
 * until a terminal status is set on the scheduler.
 */
class BrowserWriteAction final : public PacketSendAction {
 public:
  using SendHook =
      std::function<bool(DataBuffer const& data, bool* wait, bool* async,
                         BrowserWriteAction* self)>;

  BrowserWriteAction(AeContext const& ae_context, DataBuffer&& data,
                     SendHook send_hook)
      : ae_context_{ae_context},
        data_{std::move(data)},
        send_hook_{std::move(send_hook)} {}

  AE_CLASS_MOVE_ONLY(BrowserWriteAction)

  void Send() override {
    reenqueue_ = false;
    if (is_done_ || !send_hook_) {
      return;
    }
    if (awaiting_async_) {
      // Async network operation still in flight.
      reenqueue_ = true;
      return;
    }
    bool wait = false;
    bool async = false;
    bool const ok = send_hook_(data_, &wait, &async, this);
    if (!ok) {
      SetStatus(WriteAction::Status::kFail);
      return;
    }
    if (async) {
      awaiting_async_ = true;
      reenqueue_ = true;
      return;
    }
    if (wait) {
      reenqueue_ = true;
      return;
    }
    SetStatus(WriteAction::Status::kSuccess);
  }

  void Stop() noexcept override {
    is_done_ = true;
    awaiting_async_ = false;
    set_status_.Reset();
    WriteAction::SetStatus(WriteAction::Status::kStop);
  }

  bool is_done() const override { return is_done_; }
  bool re_enqueue() const override { return reenqueue_; }

  DataBuffer const& data() const noexcept { return data_; }

  void CompleteSuccess() {
    awaiting_async_ = false;
    SetStatus(WriteAction::Status::kSuccess);
  }
  void CompleteFail() {
    awaiting_async_ = false;
    SetStatus(WriteAction::Status::kFail);
  }

 protected:
  void SetStatus(WriteAction::Status status) noexcept override {
    is_done_ = true;
    awaiting_async_ = false;
    set_status_ = ae_context_.scheduler().Task(
        [this, status]() { WriteAction::SetStatus(status); });
  }

 private:
  AeContext ae_context_;
  DataBuffer data_;
  SendHook send_hook_;
  bool is_done_ = false;
  bool reenqueue_ = false;
  bool awaiting_async_ = false;
  TaskSubscription set_status_;
};

/**
 * \brief Bounded early-write / packet queue helpers shared by browser
 * transports. Compiles without __EMSCRIPTEN__ for native unit tests.
 */
class BrowserQueueState {
 public:
  explicit BrowserQueueState(std::size_t capacity) noexcept
      : capacity_{capacity} {}

  std::size_t capacity() const noexcept { return capacity_; }
  std::size_t size() const noexcept { return queued_.size(); }
  bool empty() const noexcept { return queued_.empty(); }
  bool full() const noexcept { return queued_.size() >= capacity_; }

  bool TryPush(DataBuffer&& frame) {
    if (full()) {
      return false;
    }
    queued_.push_back(std::move(frame));
    return true;
  }

  std::optional<DataBuffer> TryPop() {
    if (queued_.empty()) {
      return std::nullopt;
    }
    auto frame = std::move(queued_.front());
    queued_.erase(queued_.begin());
    return frame;
  }

  void Clear() { queued_.clear(); }

  std::vector<DataBuffer>& frames() noexcept { return queued_; }
  std::vector<DataBuffer> const& frames() const noexcept { return queued_; }

 private:
  std::size_t capacity_;
  std::vector<DataBuffer> queued_;
};

/**
 * \brief Optional native test double hooks when JS networking is unavailable.
 */
struct BrowserNetTestHooks {
  std::function<void(std::string const& url)> on_ws_connect;
  std::function<bool(DataBuffer const& data)> on_ws_send;
  std::function<void()> on_ws_close;

  std::function<std::string(std::string const& connect_url,
                            std::string const& target_json)>
      on_http_connect;
  std::function<bool(std::string const& send_url, DataBuffer const& data)>
      on_http_send;
  std::function<DataBuffer(std::string const& receive_url)> on_http_receive;
  std::function<void(std::string const& close_url)> on_http_close;
};

inline BrowserNetTestHooks& NativeBrowserNetHooks() {
  static BrowserNetTestHooks hooks;
  return hooks;
}

inline FailedWriteAction& EmplaceFailedWrite(
    AeContext const& ae_context,
    std::optional<FailedWriteAction>& slot) {
  if (!slot || slot->is_finished()) {
    slot.emplace(ae_context);
  }
  return *slot;
}

}  // namespace browser_transport_internal
}  // namespace ae

#endif  // AETHER_TRANSPORT_BROWSER_BROWSER_TRANSPORT_COMMON_H_
