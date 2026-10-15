/*
 * Copyright 2026 Aethernet Inc.
 *
 * Loopback: two P2pSafeStream instances with lossless mock P2p transports and
 * inner write limits matching a 1200 B UDP datagram budget.
 */

#include <chrono>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <vector>

#include "aether/ae_context.h"
#include "aether/client_messages/p2p_safe_message_stream.h"
#include "aether/client_messages/p2p_stream_write_budget.h"
#include "aether/config.h"
#include "aether/events/events.h"
#include "aether/safe_stream/safe_stream_config.h"
#include "aether/stream_api/istream.h"
#include "aether/write_action/failed_write_action.h"
#include "aether/write_action/write_action.h"

namespace ae {

struct LoopbackTestContext : public Env {
  template <typename... A>
  decltype(auto) Update(A&&... a) {
    return sched.Update(std::forward<A>(a)...);
  }

  void* find_component(EnvId id) noexcept override {
    if (id == EnvTypeId<TaskScheduler>::value) {
      return &sched;
    }
    if (id == EnvTypeId<EventSystem>::value) {
      return &event_system_;
    }
    return nullptr;
  }

  mutable TaskScheduler sched;
  mutable EventSystem event_system_;
};

class MockStreamWriteAction final : public WriteAction {
 public:
  explicit MockStreamWriteAction(AeContext const& context) : WriteAction{context} {
    context.scheduler().Task([&]() { SetStatus(Status::kSuccess); });
  }
};

class LoopbackP2pTransport final : public ByteStream,
                                   public std::enable_shared_from_this<
                                       LoopbackP2pTransport> {
 public:
  LoopbackP2pTransport(AeContext const& context, std::size_t max_write_bytes)
      : ByteStream{context},
        context_{context},
        stream_info_{max_write_bytes, max_write_bytes, false, LinkState::kLinked,
                     true},
        out_data_event_{context},
        stream_update_event_{context} {}

  void SetPeer(std::weak_ptr<LoopbackP2pTransport> peer) { peer_ = std::move(peer); }

  WriteAction& Write(DataBuffer&& buffer) override {
    if (buffer.size() > stream_info_.max_element_size) {
      if (!fail_action_ || fail_action_->is_finished()) {
        fail_action_.emplace(context_);
      }
      return *fail_action_;
    }
    if (auto peer = peer_.lock()) {
      peer->Deliver(DataBuffer{buffer});
    }
    if (!ok_action_ || ok_action_->is_finished()) {
      ok_action_.emplace(context_);
    }
    return *ok_action_;
  }

  StreamInfo stream_info() const override { return stream_info_; }
  StreamUpdateEvent const& stream_update_event() override {
    return stream_update_event_;
  }
  OutDataEvent const& out_data_event() override { return out_data_event_; }

  void Deliver(DataBuffer buffer) { out_data_event_.Emit(buffer); }

 private:
  AeContext context_;
  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;
  std::weak_ptr<LoopbackP2pTransport> peer_;
  std::optional<MockStreamWriteAction> ok_action_;
  std::optional<FailedWriteAction> fail_action_;
};

SafeStreamConfig BenchSafeStreamConfig() {
  return SafeStreamConfig{
      .window_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
      .max_packet_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
      .max_repeat_count = 10,
      .wait_ack_timeout = std::chrono::milliseconds{1500},
      .send_ack_timeout = std::chrono::milliseconds{0},
      .send_repeat_timeout = std::chrono::milliseconds{200},
  };
}

bool PumpUntil(LoopbackTestContext& env, std::function<bool()> done,
               int max_ticks) {
  auto epoch = TimePoint::clock::now();
  for (int i = 0; i < max_ticks; ++i) {
    if (done()) {
      return true;
    }
    env.Update(epoch += std::chrono::milliseconds{1});
  }
  return done();
}

bool RunOneMessage(std::size_t payload_bytes) {
  LoopbackTestContext env;
  AeContext ctx{env};

  auto const inner_max =
      MaxP2pStreamWritePayloadBytesForDatagramBudget(1200);

  auto link_a = std::make_shared<LoopbackP2pTransport>(ctx, inner_max);
  auto link_b = std::make_shared<LoopbackP2pTransport>(ctx, inner_max);
  link_a->SetPeer(link_b);
  link_b->SetPeer(link_a);

  auto config = BenchSafeStreamConfig();
  P2pSafeStream sender{ctx, config, link_a};
  P2pSafeStream receiver{ctx, config, link_b};

  std::vector<std::uint8_t> expect(payload_bytes);
  for (std::size_t i = 0; i < payload_bytes; ++i) {
    expect[i] = static_cast<std::uint8_t>(i & 0xFF);
  }

  DataBuffer received;
  MultiSubscription subs;
  subs += receiver.out_data_event().Subscribe([&](DataBuffer const& data) {
    received.insert(received.end(), data.begin(), data.end());
  });

  std::optional<WriteAction::Status> final_status;
  auto& wa = sender.Write(DataBuffer{expect});
  subs += wa.status_event().Subscribe(
      [&](WriteAction::Status status) { final_status = status; });

  bool ok = PumpUntil(
      env,
      [&]() {
        return final_status.has_value() &&
               received.size() >= payload_bytes;
      },
      20000);

  if (!ok || !final_status.has_value() ||
      *final_status != WriteAction::Status::kSuccess) {
    std::cerr << "payload=" << payload_bytes << " write_status="
              << (final_status.has_value()
                      ? static_cast<int>(*final_status)
                      : -1)
              << " received=" << received.size() << '\n';
    return false;
  }
  if (received.size() != payload_bytes) {
    return false;
  }
  for (std::size_t i = 0; i < payload_bytes; ++i) {
    if (received[i] != expect[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace ae

int main() {
  if (!ae::RunOneMessage(1600)) {
    std::cerr << "FAIL: loopback 1600 B\n";
    return 1;
  }
  if (!ae::RunOneMessage(3200)) {
    std::cerr << "FAIL: loopback 3200 B\n";
    return 1;
  }
  std::cout << "p2p_safe_stream_loopback_test: OK\n";
  return 0;
}
