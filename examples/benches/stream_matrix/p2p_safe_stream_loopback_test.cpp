/*
 * Copyright 2026 Aethernet Inc.
 *
 * Loopback: two P2pSafeStream instances with lossless mock P2p transports and
 * inner write limits matching a 1200 B UDP datagram budget.
 */

#include <array>
#include <chrono>
#include <cstdlib>
#include <deque>
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

#include "stream_matrix/stream_matrix_p2p_backpressure_producer.h"

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

class CoalescingLoopbackP2pTransport final : public ByteStream,
                                            public std::enable_shared_from_this<
                                                CoalescingLoopbackP2pTransport> {
 public:
  CoalescingLoopbackP2pTransport(AeContext const& context, std::size_t max_write_bytes,
                                 bool coalesce_deliver)
      : ByteStream{context},
        context_{context},
        coalesce_deliver_{coalesce_deliver},
        stream_info_{max_write_bytes, max_write_bytes, false, LinkState::kLinked,
                     true},
        out_data_event_{context},
        stream_update_event_{context} {}

  void SetPeer(std::weak_ptr<CoalescingLoopbackP2pTransport> peer) {
    peer_ = std::move(peer);
  }

  WriteAction& Write(DataBuffer&& buffer) override {
    if (coalesce_deliver_) {
      pending_.push_back(std::move(buffer));
    } else if (auto peer = peer_.lock()) {
      peer->Deliver(std::move(buffer));
    }
    if (!ok_action_ || ok_action_->is_finished()) {
      ok_action_.emplace(context_);
    }
    return *ok_action_;
  }

  void FlushCoalesced() {
    if (pending_.empty()) {
      return;
    }
    DataBuffer merged;
    for (auto& chunk : pending_) {
      merged.insert(merged.end(), chunk.begin(), chunk.end());
    }
    pending_.clear();
    if (auto peer = peer_.lock()) {
      peer->Deliver(std::move(merged));
    }
  }

  StreamInfo stream_info() const override { return stream_info_; }
  StreamUpdateEvent const& stream_update_event() override {
    return stream_update_event_;
  }
  OutDataEvent const& out_data_event() override { return out_data_event_; }

  void Deliver(DataBuffer buffer) { out_data_event_.Emit(std::move(buffer)); }

 private:
  AeContext context_;
  bool coalesce_deliver_{false};
  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;
  std::weak_ptr<CoalescingLoopbackP2pTransport> peer_;
  std::deque<DataBuffer> pending_;
  std::optional<MockStreamWriteAction> ok_action_;
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

DataBuffer MakePayload(std::size_t payload_bytes, std::uint8_t tag) {
  std::vector<std::uint8_t> expect(payload_bytes);
  for (std::size_t i = 0; i < payload_bytes; ++i) {
    expect[i] = static_cast<std::uint8_t>((i + tag * 17u) & 0xFFu);
  }
  return DataBuffer{std::move(expect)};
}

bool RunFourParallel(std::size_t payload_bytes, bool coalesce_flush) {
  LoopbackTestContext env;
  AeContext ctx{env};
  auto const inner_max =
      MaxP2pStreamWritePayloadBytesForDatagramBudget(1200);

  std::shared_ptr<CoalescingLoopbackP2pTransport> link_a;
  std::shared_ptr<CoalescingLoopbackP2pTransport> link_b;
  link_a = std::make_shared<CoalescingLoopbackP2pTransport>(ctx, inner_max,
                                                              coalesce_flush);
  link_b = std::make_shared<CoalescingLoopbackP2pTransport>(ctx, inner_max,
                                                              coalesce_flush);
  link_a->SetPeer(link_b);
  link_b->SetPeer(link_a);

  auto config = BenchSafeStreamConfig();
  P2pSafeStream sender{ctx, config, link_a};
  P2pSafeStream receiver{ctx, config, link_b};

  std::array<std::vector<std::uint8_t>, 4> received{};
  MultiSubscription subs;
  subs += receiver.out_data_event().Subscribe([&](DataBuffer const& data) {
    if (data.empty()) {
      return;
    }
    std::uint8_t tag = data[0] % 4;
    received[tag].insert(received[tag].end(), data.begin(), data.end());
  });

  std::array<std::optional<WriteAction::Status>, 4> final_status{};
  for (int i = 0; i < 4; ++i) {
    auto& wa = sender.Write(MakePayload(payload_bytes, static_cast<std::uint8_t>(i)));
    subs += wa.status_event().Subscribe([&, i](WriteAction::Status status) {
      final_status[static_cast<std::size_t>(i)] = status;
    });
  }

  auto done = [&]() {
    for (int i = 0; i < 4; ++i) {
      if (!final_status[static_cast<std::size_t>(i)].has_value() ||
          *final_status[static_cast<std::size_t>(i)] != WriteAction::Status::kSuccess) {
        return false;
      }
    }
    for (int i = 0; i < 4; ++i) {
      if (received[static_cast<std::size_t>(i)].size() < payload_bytes) {
        return false;
      }
    }
    return true;
  };

  auto epoch = TimePoint::clock::now();
  for (int tick = 0; tick < 30000; ++tick) {
    env.Update(epoch += std::chrono::milliseconds{1});
    if (coalesce_flush && tick % 5 == 0) {
      link_a->FlushCoalesced();
      link_b->FlushCoalesced();
    }
    if (done()) {
      break;
    }
  }
  link_a->FlushCoalesced();
  link_b->FlushCoalesced();
  env.Update(epoch += std::chrono::milliseconds{1});

  if (!done()) {
    std::cerr << "FAIL four parallel coalesce=" << coalesce_flush << '\n';
    return false;
  }
  for (int i = 0; i < 4; ++i) {
    auto expect = MakePayload(payload_bytes, static_cast<std::uint8_t>(i));
    auto const& got = received[static_cast<std::size_t>(i)];
    if (got.size() != payload_bytes ||
        !std::equal(got.begin(), got.end(), expect.begin())) {
      std::cerr << "FAIL content tag=" << i << '\n';
      return false;
    }
  }
  return true;
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

bool RunTryWriteWouldBlockPreservesBuffer() {
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
  auto const max_chunk = sender.stream_info().max_element_size;
  if (max_chunk == 0) {
    return false;
  }
  std::vector<std::uint8_t> pat(max_chunk, 0xCD);
  DataBuffer chunk{pat};
  auto epoch = TimePoint::clock::now();
  for (int guard = 0; guard < 100000 && sender.CanWritePayload(chunk.size());
       ++guard) {
    if (sender.TryWrite(chunk).result != P2pWriteOfferResult::kAccepted) {
      return false;
    }
    env.Update(epoch += std::chrono::milliseconds{1});
  }
  if (sender.CanWritePayload(chunk.size())) {
    return false;
  }
  DataBuffer fingerprint = pat;
  return sender.TryWrite(chunk).result == P2pWriteOfferResult::kWouldBlock &&
         chunk == fingerprint;
}

struct BackpressureRunStats {
  bool ok{false};
  std::uint64_t would_block{0};
  std::uint64_t resumes{0};
  std::size_t peak_buffer{0};
  std::size_t residual{0};
};

BackpressureRunStats RunProducerOnce(std::size_t payload_bytes, int delay_ms,
                                     int drop_every_n) {
  BackpressureRunStats out{};
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

  std::vector<std::uint8_t> payload(payload_bytes);
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<std::uint8_t>((i * 131) ^ 0xA5);
  }
  std::vector<std::uint8_t> rx;
  Subscription sub = receiver.out_data_event().Subscribe(
      [&](DataBuffer const& data) {
        rx.insert(rx.end(), data.begin(), data.end());
      });

  auto const expected = payload;
  stream_matrix_bench::P2pBackpressureProducer producer(ctx, sender,
                                                          std::move(payload));
  bool done = false;
  bool success = false;
  producer.Start(sender.stream_info().max_element_size, [&](bool ok) {
    success = ok;
    done = true;
  });
  auto epoch = TimePoint::clock::now();
  for (int tick = 0; tick < 800000 && !done; ++tick) {
    env.Update(epoch += std::chrono::milliseconds{1});
    producer.Pump();
  }
  auto const stats = producer.stats();
  out.would_block = stats.would_block_count;
  out.resumes = stats.resume_count;
  out.peak_buffer = stats.peak_send_buffer_used;
  out.residual = stats.residual_send_buffer_used;
  out.ok = done && success && rx.size() == payload_bytes && rx == expected;
  sub.Reset();
  return out;
}

}  // namespace ae

int main() {
  if (!ae::RunFourParallel(1024, false)) {
    std::cerr << "FAIL: loopback four parallel 1024 B\n";
    return 1;
  }
  if (!ae::RunFourParallel(1024, true)) {
    std::cerr << "FAIL: loopback four parallel coalesced flush 1024 B\n";
    return 1;
  }
  if (!ae::RunOneMessage(1600)) {
    std::cerr << "FAIL: loopback 1600 B\n";
    return 1;
  }
  if (!ae::RunOneMessage(3200)) {
    std::cerr << "FAIL: loopback 3200 B\n";
    return 1;
  }
  if (!ae::RunTryWriteWouldBlockPreservesBuffer()) {
    std::cerr << "FAIL: TryWrite would_block preserves buffer\n";
    return 1;
  }
  std::uint64_t total_wb = 0;
  std::uint64_t total_resume = 0;
  std::size_t peak_buf = 0;
  for (int i = 0; i < 10; ++i) {
    auto r = ae::RunProducerOnce(1024 * 1024, 2, 0);
    if (!r.ok) {
      std::cerr << "FAIL: backpressure producer 1MiB run " << i << '\n';
      return 1;
    }
    total_wb += r.would_block;
    total_resume += r.resumes;
    peak_buf = std::max(peak_buf, r.peak_buffer);
    std::cout << "bp_transfer i=" << i << " wb=" << r.would_block
              << " resume=" << r.resumes << " peak=" << r.peak_buffer
              << " residual=" << r.residual << '\n';
  }
  auto lossy = ae::RunProducerOnce(256 * 1024, 5, 17);
  if (!lossy.ok || lossy.would_block == 0 || lossy.resumes == 0) {
    std::cerr << "FAIL: lossy backpressure path\n";
    return 1;
  }
  std::cout << "backpressure_summary would_block=" << total_wb
            << " resumes=" << total_resume << " peak_buffer=" << peak_buf << '\n';
  std::cout << "p2p_safe_stream_loopback_test: OK\n";
  return 0;
}
