/*
 * P2pBackpressureProducer failure, completion, and destroy-from-on_done checks.
 */
#include <atomic>
#include <chrono>
#include <cstdint>
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

#include "stream_matrix/stream_matrix_p2p_backpressure_producer.h"

namespace ae {
namespace {

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
    context.scheduler().Task([this]() { SetStatus(Status::kSuccess); });
  }
};

class LoopbackP2pTransport final : public ByteStream,
                                   public std::enable_shared_from_this<LoopbackP2pTransport> {
 public:
  LoopbackP2pTransport(AeContext const& context, std::size_t max_write_bytes)
      : ByteStream{context},
        context_{context},
        stream_info_{max_write_bytes, max_write_bytes, false, LinkState::kLinked,
                     true},
        out_data_event_{context},
        stream_update_event_{context} {}

  void SetPeer(std::weak_ptr<LoopbackP2pTransport> peer) { peer_ = std::move(peer); }

  void SetLinkState(LinkState state, bool writable) {
    stream_info_.link_state = state;
    stream_info_.is_writable = writable;
    stream_update_event_.Emit();
  }

  void FailAfterWireWrites(std::uint64_t n) { fail_after_writes_ = n; }

  WriteAction& Write(DataBuffer&& buffer) override {
    ++wire_writes_;
    if (fail_after_writes_ > 0 && wire_writes_ >= fail_after_writes_) {
      if (!fail_action_ || fail_action_->is_finished()) {
        fail_action_.emplace(context_);
      }
      return *fail_action_;
    }
    if (auto peer = peer_.lock()) {
      peer->Deliver(std::move(buffer));
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

  void Deliver(DataBuffer buffer) { out_data_event_.Emit(std::move(buffer)); }

 private:
  AeContext context_;
  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;
  std::weak_ptr<LoopbackP2pTransport> peer_;
  std::optional<MockStreamWriteAction> ok_action_;
  std::optional<FailedWriteAction> fail_action_;
  std::uint64_t wire_writes_{0};
  std::uint64_t fail_after_writes_{0};
};

SafeStreamConfig BenchConfig() {
  return SafeStreamConfig{
      .window_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
      .max_packet_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
      .max_repeat_count = 12,
      .wait_ack_timeout = std::chrono::milliseconds{1500},
      .send_ack_timeout = std::chrono::milliseconds{0},
      .send_repeat_timeout = std::chrono::milliseconds{50},
  };
}

SafeStreamConfig TightConfig() {
  return SafeStreamConfig{
      .window_size = 256,
      .max_packet_size = 128,
      .max_repeat_count = 8,
      .wait_ack_timeout = std::chrono::milliseconds{1500},
      .send_ack_timeout = std::chrono::milliseconds{0},
      .send_repeat_timeout = std::chrono::milliseconds{50},
  };
}

std::vector<std::uint8_t> MakePayload(std::size_t bytes, std::uint8_t tag) {
  std::vector<std::uint8_t> payload(bytes);
  for (std::size_t i = 0; i < payload.size(); ++i) {
    payload[i] = static_cast<std::uint8_t>((i + tag * 13u) & 0xFFu);
  }
  return payload;
}

bool PumpUntil(LoopbackTestContext& env, std::function<bool()> pred, int max_ticks) {
  auto epoch = TimePoint::clock::now();
  for (int i = 0; i < max_ticks; ++i) {
    if (pred()) {
      return true;
    }
    env.Update(epoch += std::chrono::milliseconds{1});
  }
  return pred();
}

void PumpExtra(LoopbackTestContext& env, int ticks) {
  auto epoch = TimePoint::clock::now();
  for (int i = 0; i < ticks; ++i) {
    env.Update(epoch += std::chrono::milliseconds{1});
  }
}

struct DestroyObservation {
  std::weak_ptr<stream_matrix_bench::P2pBackpressureProducer> weak;
  std::uint64_t done_calls{0};
  bool done{false};
  bool ok{true};
};

bool RunDestroyFromOnDone(
    LoopbackTestContext& env, P2pSafeStream& sender,
    std::vector<std::uint8_t> payload,
    std::function<void(P2pSafeStream& sender, DestroyObservation&)> const& before_wait) {
  AeContext ctx{env};
  DestroyObservation obs;
  auto producer = std::make_shared<stream_matrix_bench::P2pBackpressureProducer>(
      ctx, sender, std::move(payload));
  obs.weak = producer;
  producer->Start(sender.stream_info().max_element_size,
                  [producer = std::move(producer), &obs](bool success) mutable {
                    obs.ok = success;
                    obs.done = true;
                    obs.done_calls = producer->done_calls();
                    producer.reset();
                  });
  if (before_wait) {
    before_wait(sender, obs);
  }
  PumpUntil(env, [&] { return obs.done; }, 400000);
  PumpExtra(env, 800);
  return obs.done && obs.done_calls == 1 && obs.weak.expired();
}

}  // namespace
}  // namespace ae

static int g_failures = 0;

static void Expect(bool ok, char const* msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << '\n';
    ++g_failures;
  }
}

int main() {
  ae::LoopbackTestContext env;
  ae::AeContext ctx{env};
  auto const inner_max = ae::MaxP2pStreamWritePayloadBytesForDatagramBudget(1200);

  {
    auto link_a = std::make_shared<ae::LoopbackP2pTransport>(ctx, inner_max);
    auto link_b = std::make_shared<ae::LoopbackP2pTransport>(ctx, inner_max);
    link_a->SetPeer(link_b);
    link_b->SetPeer(link_a);
    link_a->SetLinkState(ae::LinkState::kLinkError, false);
    ae::P2pSafeStream sender{ctx, ae::BenchConfig(), link_a};
    ae::P2pSafeStream receiver{ctx, ae::BenchConfig(), link_b};
    (void)receiver;
    Expect(ae::RunDestroyFromOnDone(env, sender, ae::MakePayload(4096, 1), {}),
           "destroy after link error before first write");
  }

  {
    auto link_a = std::make_shared<ae::LoopbackP2pTransport>(ctx, inner_max);
    auto link_b = std::make_shared<ae::LoopbackP2pTransport>(ctx, inner_max);
    link_a->SetPeer(link_b);
    link_b->SetPeer(link_a);
    ae::P2pSafeStream sender{ctx, ae::TightConfig(), link_a};
    ae::P2pSafeStream receiver{ctx, ae::TightConfig(), link_b};
    (void)receiver;
    Expect(ae::RunDestroyFromOnDone(
               env, sender, ae::MakePayload(64 * 1024, 2),
               [link_a, &env](ae::P2pSafeStream&, ae::DestroyObservation& obs) {
                 ae::PumpUntil(env,
                               [&] {
                                 if (auto p = obs.weak.lock()) {
                                   return p->stats().would_block_count > 0;
                                 }
                                 return false;
                               },
                               50000);
                 link_a->SetLinkState(ae::LinkState::kLinkError, false);
               }),
           "destroy after link error while blocked");
  }

  {
    auto link_a = std::make_shared<ae::LoopbackP2pTransport>(ctx, inner_max);
    auto link_b = std::make_shared<ae::LoopbackP2pTransport>(ctx, inner_max);
    link_a->SetPeer(link_b);
    link_b->SetPeer(link_a);
    link_a->FailAfterWireWrites(6);
    ae::P2pSafeStream sender{ctx, ae::BenchConfig(), link_a};
    ae::P2pSafeStream receiver{ctx, ae::BenchConfig(), link_b};
    (void)receiver;
    Expect(ae::RunDestroyFromOnDone(env, sender, ae::MakePayload(32 * 1024, 3), {}),
           "destroy after terminal fail on accepted write");
  }

  {
    auto link_a = std::make_shared<ae::LoopbackP2pTransport>(ctx, inner_max);
    auto link_b = std::make_shared<ae::LoopbackP2pTransport>(ctx, inner_max);
    link_a->SetPeer(link_b);
    link_b->SetPeer(link_a);
    ae::P2pSafeStream sender{ctx, ae::BenchConfig(), link_a};
    ae::P2pSafeStream receiver{ctx, ae::BenchConfig(), link_b};
    (void)receiver;
    Expect(ae::RunDestroyFromOnDone(env, sender, ae::MakePayload(8 * 1024, 4), {}),
           "destroy after successful last write");
  }

  if (g_failures != 0) {
    std::cerr << "p2p_safe_stream_backpressure_producer_fail_test: " << g_failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "p2p_safe_stream_backpressure_producer_fail_test: OK\n";
  return 0;
}
