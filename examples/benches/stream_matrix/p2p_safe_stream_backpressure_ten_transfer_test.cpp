/*
 * Ten sequential 1 MiB transfers on one P2pSafeStream pair (backpressure).
 */

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <deque>
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

struct TransportStats {
  std::uint64_t wire_writes{0};
  std::uint64_t delayed_enqueued{0};
  std::uint64_t dropped{0};
  std::uint64_t delivered{0};
};

class InstrumentedP2pTransport final
    : public ByteStream,
      public std::enable_shared_from_this<InstrumentedP2pTransport> {
 public:
  InstrumentedP2pTransport(AeContext const& context, std::size_t max_write_bytes,
                           int delay_ms, int drop_every_n, TransportStats& stats)
      : ByteStream{context},
        context_{context},
        delay_ms_{delay_ms},
        drop_every_n_{drop_every_n},
        stats_{stats},
        stream_info_{max_write_bytes, max_write_bytes, false, LinkState::kLinked,
                     true},
        out_data_event_{context},
        stream_update_event_{context} {}

  void SetPeer(std::weak_ptr<InstrumentedP2pTransport> peer) {
    peer_ = std::move(peer);
  }

  void SetLinked(bool linked) {
    stream_info_.link_state = linked ? LinkState::kLinked : LinkState::kUnlinked;
    stream_info_.is_writable = linked;
    stream_update_event_.Emit();
  }

  void SetPermanentLinkError() {
    stream_info_.link_state = LinkState::kLinkError;
    stream_info_.is_writable = false;
    stream_update_event_.Emit();
  }

  WriteAction& Write(DataBuffer&& buffer) override {
    ++stats_.wire_writes;
    ++seq_;
    bool const drop = drop_every_n_ > 0 && (seq_ % drop_every_n_) == 0;
    if (drop) {
      ++stats_.dropped;
      if (!ok_action_ || ok_action_->is_finished()) {
        ok_action_.emplace(context_);
      }
      return *ok_action_;
    }
    if (delay_ms_ <= 0) {
      DeliverPeer(std::move(buffer));
    } else {
      ++stats_.delayed_enqueued;
      pending_.push_back(std::move(buffer));
      ScheduleDrain();
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

 private:
  void ScheduleDrain() {
    if (drain_scheduled_) {
      return;
    }
    drain_scheduled_ = true;
    context_.scheduler().DelayedTask(
        [self = shared_from_this()]() {
          self->drain_scheduled_ = false;
          if (self->pending_.empty()) {
            return;
          }
          auto buf = std::move(self->pending_.front());
          self->pending_.pop_front();
          self->DeliverPeer(std::move(buf));
          if (!self->pending_.empty()) {
            self->ScheduleDrain();
          }
        },
        std::chrono::milliseconds{delay_ms_});
  }

  void DeliverPeer(DataBuffer buffer) {
    ++stats_.delivered;
    if (auto peer = peer_.lock()) {
      peer->out_data_event_.Emit(std::move(buffer));
    }
  }

  AeContext context_;
  int delay_ms_{0};
  int drop_every_n_{0};
  int seq_{0};
  bool drain_scheduled_{false};
  TransportStats& stats_;
  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;
  std::weak_ptr<InstrumentedP2pTransport> peer_;
  std::deque<DataBuffer> pending_;
  std::optional<MockStreamWriteAction> ok_action_;
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

std::vector<std::uint8_t> MakePayload(std::size_t bytes, std::uint32_t transfer_id) {
  std::vector<std::uint8_t> payload(bytes);
  payload[0] = static_cast<std::uint8_t>(transfer_id);
  payload[1] = static_cast<std::uint8_t>(transfer_id >> 8);
  for (std::size_t i = 2; i < payload.size(); ++i) {
    payload[i] = static_cast<std::uint8_t>((i * 131u) ^ (transfer_id * 17u) ^ 0xA5u);
  }
  return payload;
}

struct TransferRow {
  std::uint32_t transfer_id{};
  std::size_t rx_bytes{0};
  bool integrity{false};
  bool success{false};
  std::uint64_t accepted{0};
  std::uint64_t success_t{0};
  std::uint64_t fail_t{0};
  std::uint64_t stop_t{0};
  std::uint64_t would_block{0};
  std::uint64_t resumes{0};
  std::size_t peak_in_flight{0};
  std::size_t peak_buffer{0};
  std::size_t peak_buffer_terminal{0};
  std::size_t buffer_after_drain{0};
  std::size_t events_before{0};
  std::size_t events_peak{0};
  std::size_t events_after{0};
  std::size_t handlers_before{0};
  std::size_t handlers_peak{0};
  std::size_t handlers_after{0};
};

struct EventPoolUsage {
  std::size_t registered_events{};
  std::size_t handler_relationships{};
};

EventPoolUsage Usage(LoopbackTestContext const& env) {
  auto const u = env.event_system_.Usage();
  return {u.registered_event_keys, u.handler_relationships};
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
  ae::TransportStats tx_stats{};
  auto const inner_max = ae::MaxP2pStreamWritePayloadBytesForDatagramBudget(1200);
  auto link_a =
      std::make_shared<ae::InstrumentedP2pTransport>(ctx, inner_max, 2, 17, tx_stats);
  ae::TransportStats rx_side_stats{};
  auto link_b = std::make_shared<ae::InstrumentedP2pTransport>(ctx, inner_max, 0, 0,
                                                               rx_side_stats);
  link_a->SetPeer(link_b);
  link_b->SetPeer(link_a);
  auto config = ae::BenchConfig();
  ae::P2pSafeStream sender{ctx, config, link_a};
  ae::P2pSafeStream receiver{ctx, config, link_b};

  std::vector<std::uint8_t> rx;
  ae::Subscription rx_sub = receiver.out_data_event().Subscribe(
      [&](ae::DataBuffer const& data) {
        rx.insert(rx.end(), data.begin(), data.end());
      });

  std::vector<ae::TransferRow> rows;
  rows.reserve(10);

  for (std::uint32_t transfer = 0; transfer < 10; ++transfer) {
    ae::TransferRow row{.transfer_id = transfer};
    row.events_before = ae::Usage(env).registered_events;
    row.handlers_before = ae::Usage(env).handler_relationships;
    row.events_peak = row.events_before;
    row.handlers_peak = row.handlers_before;

    rx.clear();
    auto payload = ae::MakePayload(1024 * 1024, transfer);
    auto const expected = payload;
    bool done = false;
    bool ok = false;

    ae::stream_matrix_bench::P2pBackpressureProducer producer(ctx, sender,
                                                                std::move(payload));
    producer.Start(sender.stream_info().max_element_size, [&](bool success) {
      ok = success;
      done = true;
    });

    auto epoch = ae::TimePoint::clock::now();
    for (int tick = 0; tick < 1200000 && !done; ++tick) {
      env.Update(epoch += std::chrono::milliseconds{1});
      auto const u = ae::Usage(env);
      row.events_peak = std::max(row.events_peak, u.registered_events);
      row.handlers_peak = std::max(row.handlers_peak, u.handler_relationships);
      if (transfer == 7 && tick == 18000) {
        link_a->SetLinked(false);
      }
      if (transfer == 7 && tick == 28000) {
        link_a->SetLinked(true);
      }
    }

    for (int drain = 0; drain < 5000; ++drain) {
      env.Update(epoch += std::chrono::milliseconds{1});
    }

    auto const stats = producer.stats();
    row.rx_bytes = rx.size();
    row.integrity = row.rx_bytes == expected.size() && rx == expected;
    row.success = done && ok && row.integrity;
    row.accepted = stats.accepted_writes;
    row.success_t = stats.success_terminals;
    row.fail_t = stats.fail_terminals;
    row.stop_t = stats.stop_terminals;
    row.would_block = stats.would_block_count;
    row.resumes = stats.resume_count;
    row.peak_in_flight = stats.peak_in_flight_writes;
    row.peak_buffer = stats.peak_send_buffer_used;
    row.peak_buffer_terminal = stats.peak_send_buffer_in_terminal;
    row.buffer_after_drain = sender.send_buffer_used_bytes();
    row.events_after = ae::Usage(env).registered_events;
    row.handlers_after = ae::Usage(env).handler_relationships;
    rows.push_back(row);

    Expect(row.success, "transfer success");
    Expect(row.buffer_after_drain == 0, "send buffer drained after transfer");
    std::cout << "transfer=" << transfer << " rx=" << row.rx_bytes
              << " ok=" << (row.success ? 1 : 0) << " wb=" << row.would_block
              << " resume=" << row.resumes << " peak_buf=" << row.peak_buffer
              << " drain_buf=" << row.buffer_after_drain << " events "
              << row.events_before << '/' << row.events_peak << '/'
              << row.events_after << " handlers " << row.handlers_before << '/'
              << row.handlers_peak << '/' << row.handlers_after << '\n';
  }

  rx_sub.Reset();

  Expect(tx_stats.dropped > 0, "transport drops observed");
  Expect(tx_stats.delayed_enqueued > 0, "transport delay observed");
  Expect(tx_stats.delivered > tx_stats.dropped, "safe stream recovered losses");

  if (rows.size() >= 2) {
    Expect(rows.back().events_after <= rows.front().events_before + 4,
           "events stable across ten transfers");
    Expect(rows.back().handlers_after <= rows.front().handlers_before + 8,
           "handlers stable across ten transfers");
  }

  {
    link_a->SetPermanentLinkError();
    auto epoch = ae::TimePoint::clock::now();
    for (int tick = 0; tick < 500; ++tick) {
      env.Update(epoch += std::chrono::milliseconds{1});
    }
    ae::DataBuffer probe(16, 0xEE);
    auto const offer = sender.TryWrite(probe);
    Expect(offer.result == ae::P2pWriteOfferResult::kError,
           "permanent link error surfaces as TryWrite ERROR");
    Expect(probe == ae::DataBuffer(16, 0xEE), "ERROR does not mutate caller buffer");
  }

  if (g_failures != 0) {
    std::cerr << "p2p_safe_stream_backpressure_ten_transfer_test: " << g_failures
              << " failure(s)\n";
    return 1;
  }

  std::cout << "transport wire=" << tx_stats.wire_writes << " delayed="
            << tx_stats.delayed_enqueued << " dropped=" << tx_stats.dropped
            << " delivered=" << tx_stats.delivered << '\n';
  std::cout << "p2p_safe_stream_backpressure_ten_transfer_test: OK\n";
  return 0;
}
