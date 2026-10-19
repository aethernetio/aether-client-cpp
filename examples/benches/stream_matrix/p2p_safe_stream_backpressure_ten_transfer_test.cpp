/*
 * Ten sequential 1 MiB transfers on one P2pSafeStream pair (backpressure).
 */

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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
  std::uint64_t delivery_paused{0};
  std::uint64_t blocked_while_paused{0};
  std::uint64_t pending_dropped_while_paused{0};
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
    delivery_enabled_ = linked;
    stream_info_.link_state = linked ? LinkState::kLinked : LinkState::kUnlinked;
    stream_info_.is_writable = linked;
    if (!linked) {
      ++stats_.delivery_paused;
      TrimPendingWhilePaused();
    } else {
      ScheduleDrain();
    }
    stream_update_event_.Emit();
  }

  void SetPermanentLinkError() {
    delivery_enabled_ = false;
    stream_info_.link_state = LinkState::kLinkError;
    stream_info_.is_writable = false;
    pending_.clear();
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
    if (!delivery_enabled_) {
      if (pending_.size() >= kMaxPendingWhilePaused) {
        ++stats_.pending_dropped_while_paused;
      } else {
        ++stats_.delayed_enqueued;
        pending_.push_back(std::move(buffer));
      }
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
  static constexpr std::size_t kMaxPendingWhilePaused = 32;

  void TrimPendingWhilePaused() {
    while (pending_.size() > kMaxPendingWhilePaused) {
      pending_.pop_front();
      ++stats_.pending_dropped_while_paused;
    }
  }

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
    if (!delivery_enabled_ || stream_info_.link_state != LinkState::kLinked) {
      ++stats_.blocked_while_paused;
      return;
    }
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
  bool delivery_enabled_{true};
  TransportStats& stats_;
  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;
  std::weak_ptr<InstrumentedP2pTransport> peer_;
  std::deque<DataBuffer> pending_;
  std::optional<MockStreamWriteAction> ok_action_;
};

SafeStreamConfig BenchConfigStrict() {
  return SafeStreamConfig{
      .window_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
      .max_packet_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
      .max_repeat_count = 12,
      .wait_ack_timeout =
          std::chrono::duration_cast<ae::Duration>(std::chrono::milliseconds{1500}),
      .send_ack_timeout = ae::Duration{0},
      .send_repeat_timeout =
          std::chrono::duration_cast<ae::Duration>(std::chrono::milliseconds{50}),
  };
}

SafeStreamConfig BenchConfigDiagnostic() {
  return SafeStreamConfig{
      .window_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
      .max_packet_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
      .max_repeat_count = 255,
      .wait_ack_timeout =
          std::chrono::duration_cast<ae::Duration>(std::chrono::milliseconds{60000}),
      .send_ack_timeout = ae::Duration{0},
      .send_repeat_timeout =
          std::chrono::duration_cast<ae::Duration>(std::chrono::milliseconds{50}),
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
  std::uint64_t done_calls{0};
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

static std::int64_t ConfigMs(ae::Duration d) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

static void Expect(bool ok, char const* msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << '\n';
    ++g_failures;
  }
}

enum class TenTransferRunMode { kAll, kStrict, kDiagnostic };

static TenTransferRunMode ParseRunMode() {
  char const* v = std::getenv("AETHER_STREAM_MATRIX_TEN_TRANSFER_PROFILE");
  if (v == nullptr || v[0] == '\0' || std::strcmp(v, "all") == 0) {
    return TenTransferRunMode::kAll;
  }
  if (std::strcmp(v, "strict") == 0) {
    return TenTransferRunMode::kStrict;
  }
  if (std::strcmp(v, "diagnostic") == 0) {
    return TenTransferRunMode::kDiagnostic;
  }
  std::cerr << "Unknown AETHER_STREAM_MATRIX_TEN_TRANSFER_PROFILE=" << v << '\n';
  return TenTransferRunMode::kAll;
}

static bool RunTenTransferProfile(ae::SafeStreamConfig config, char const* profile_name,
                                  bool enforce_success, std::size_t* complete_count_out) {
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
  ae::P2pSafeStream sender{ctx, config, link_a};
  ae::P2pSafeStream receiver{ctx, config, link_b};
  std::cout << "profile=" << profile_name << " wait_ack_ms=" << ConfigMs(config.wait_ack_timeout)
            << " max_repeat=" << static_cast<unsigned>(config.max_repeat_count)
            << " send_repeat_ms=" << ConfigMs(config.send_repeat_timeout) << '\n';

  std::vector<std::uint8_t> rx;
  ae::Subscription rx_sub = receiver.out_data_event().Subscribe(
      [&](ae::DataBuffer const& data) {
        rx.insert(rx.end(), data.begin(), data.end());
      });

  std::vector<ae::TransferRow> rows;
  rows.reserve(10);
  bool all_success = true;
  bool first_fail_logged = false;
  ae::TimePoint epoch = ae::TimePoint::clock::now();

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
    int last_progress_tick = 0;
    std::size_t last_rx_progress = 0;

    auto producer = std::make_shared<ae::stream_matrix_bench::P2pBackpressureProducer>(
        ctx, sender, std::move(payload));
    producer->Start(sender.stream_info().max_element_size, [producer, &ok, &done](bool success) {
      ok = success;
      done = true;
    });

    bool unlink_done = false;
    bool relink_done = false;
    std::uint64_t delivered_at_unlink = 0;
    std::uint64_t delivered_at_relink = 0;
    std::size_t offset_at_unlink = 0;
    int unlink_tick = 0;
    for (int tick = 0; tick < 1200000 && !done; ++tick) {
      env.Update(epoch += std::chrono::milliseconds{1});
      if (rx.size() > last_rx_progress) {
        last_rx_progress = rx.size();
        last_progress_tick = tick;
      }
      auto const u = ae::Usage(env);
      row.events_peak = std::max(row.events_peak, u.registered_events);
      row.handlers_peak = std::max(row.handlers_peak, u.handler_relationships);
      if (transfer == 7 && !unlink_done && producer->offset() > 200000 &&
          !producer->finished()) {
        delivered_at_unlink = tx_stats.delivered;
        offset_at_unlink = producer->offset();
        unlink_tick = tick;
        link_a->SetLinked(false);
        unlink_done = true;
      }
      if (enforce_success && transfer == 7 && unlink_done && !relink_done) {
        Expect(tx_stats.delivered == delivered_at_unlink,
               "no peer delivery while unlinked");
      }
      if (transfer == 7 && unlink_done && !relink_done &&
          tick - unlink_tick > 200) {
        delivered_at_relink = tx_stats.delivered;
        link_a->SetLinked(true);
        relink_done = true;
      }
    }
    if (transfer == 7 && unlink_done && !relink_done) {
      link_a->SetLinked(true);
      relink_done = true;
    }
    if (transfer == 7 && enforce_success) {
      Expect(unlink_done, "unlink triggered by transfer progress");
      Expect(relink_done, "relink after progress");
      Expect(offset_at_unlink > 0 && offset_at_unlink < 1024 * 1024,
             "transfer incomplete at unlink");
      Expect(tx_stats.delivered > delivered_at_relink,
             "delivery resumed after relink");
    }

    for (int drain = 0; drain < 5000; ++drain) {
      env.Update(epoch += std::chrono::milliseconds{1});
    }

    auto const stats = producer->stats();
    row.done_calls = producer->done_calls();
    row.rx_bytes = rx.size();
    bool const pending = !done;
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

    if (!row.success) {
      all_success = false;
      if (!first_fail_logged) {
        first_fail_logged = true;
      std::cout << "profile=" << profile_name << " first_fail transfer=" << transfer
                << " rx=" << row.rx_bytes << " integrity=" << (row.integrity ? 1 : 0)
                << " fail_t=" << row.fail_t << " stop_t=" << row.stop_t
                << " pending=" << (pending ? 1 : 0) << " last_progress_tick=" << last_progress_tick
                << " unlink_offset=" << offset_at_unlink << " unlink_tick=" << unlink_tick
                << " ack_budget_ms=" << ConfigMs(config.wait_ack_timeout)
                << " max_repeat=" << static_cast<unsigned>(config.max_repeat_count)
                << " repeat_period_ms=" << ConfigMs(config.send_repeat_timeout)
                << " success_t=" << row.success_t << " accepted=" << row.accepted << '\n';
      }
    }
    if (enforce_success) {
      Expect(row.success, "transfer success");
      Expect(row.done_calls == 1, "exactly one on_done");
      Expect(row.buffer_after_drain == 0, "send buffer drained after transfer");
    }
    std::cout << "transfer=" << transfer << " rx=" << row.rx_bytes
              << " integrity=" << (row.integrity ? 1 : 0)
              << " ok=" << (row.success ? 1 : 0) << " fail_t=" << row.fail_t
              << " stop_t=" << row.stop_t << " pending=" << (pending ? 1 : 0)
              << " done=" << row.done_calls << " wb=" << row.would_block
              << " resume=" << row.resumes << " peak_buf=" << row.peak_buffer
              << " drain_buf=" << row.buffer_after_drain << " events "
              << row.events_before << '/' << row.events_peak << '/'
              << row.events_after << " handlers " << row.handlers_before << '/'
              << row.handlers_peak << '/' << row.handlers_after << '\n';
  }

  std::size_t complete_count = 0;
  for (auto const& row : rows) {
    if (row.success) {
      ++complete_count;
    }
  }
  if (complete_count_out != nullptr) {
    *complete_count_out = complete_count;
  }

  rx_sub.Reset();

  if (enforce_success) {
    Expect(tx_stats.dropped > 0, "transport drops observed");
    Expect(tx_stats.delayed_enqueued > 0, "transport delay observed");
    Expect(tx_stats.delivered > tx_stats.dropped, "safe stream recovered losses");

    if (rows.size() >= 2) {
      Expect(rows.back().events_after <= rows.front().events_before + 4,
             "events stable across ten transfers");
      Expect(rows.back().handlers_after <= rows.front().handlers_before + 8,
             "handlers stable across ten transfers");
    }

    Expect(tx_stats.delivery_paused > 0, "delivery pause events");
    Expect(tx_stats.blocked_while_paused > 0 || tx_stats.pending_dropped_while_paused > 0,
           "pause blocked or trimmed pending");
  }

  std::cout << "profile=" << profile_name << " transport wire=" << tx_stats.wire_writes
            << " delivered=" << tx_stats.delivered << " all_success="
            << (all_success ? 1 : 0) << '\n';
  return all_success;
}

int main() {
  TenTransferRunMode const mode = ParseRunMode();
  bool strict_ok = true;
  bool diagnostic_ok = true;
  std::size_t strict_complete = 0;
  std::size_t diagnostic_complete = 0;

  if (mode != TenTransferRunMode::kDiagnostic) {
    strict_ok = RunTenTransferProfile(ae::BenchConfigStrict(), "strict", false,
                                      &strict_complete);
    std::cout << "TEN_TRANSFER_PROFILE_RESULT profile=strict complete=" << strict_complete
              << "/10 pass=" << (strict_ok ? 1 : 0) << '\n';
    if (mode == TenTransferRunMode::kStrict && !strict_ok) {
      ++g_failures;
    }
  }
  if (mode != TenTransferRunMode::kStrict) {
    diagnostic_ok = RunTenTransferProfile(ae::BenchConfigDiagnostic(), "diagnostic", true,
                                            &diagnostic_complete);
    std::cout << "TEN_TRANSFER_PROFILE_RESULT profile=diagnostic complete="
              << diagnostic_complete << "/10 pass=" << (diagnostic_ok ? 1 : 0) << '\n';
    if (!diagnostic_ok) {
      ++g_failures;
    }
  }

  if (mode != TenTransferRunMode::kStrict) {
    ae::LoopbackTestContext fail_env;
    ae::AeContext fail_ctx{fail_env};
    ae::TransportStats fail_stats{};
    auto const inner_max = ae::MaxP2pStreamWritePayloadBytesForDatagramBudget(1200);
    auto fail_a = std::make_shared<ae::InstrumentedP2pTransport>(
        fail_ctx, inner_max, 0, 0, fail_stats);
    auto fail_b = std::make_shared<ae::InstrumentedP2pTransport>(
        fail_ctx, inner_max, 0, 0, fail_stats);
    fail_a->SetPeer(fail_b);
    fail_b->SetPeer(fail_a);
    ae::P2pSafeStream fail_sender{fail_ctx, ae::BenchConfigDiagnostic(), fail_a};
    ae::P2pSafeStream fail_receiver{fail_ctx, ae::BenchConfigDiagnostic(), fail_b};
    (void)fail_receiver;

    bool disc_done = false;
    bool disc_ok = true;
    std::uint64_t disc_done_calls = 0;
    auto payload = ae::MakePayload(64 * 1024, 1000u);
    auto producer = std::make_shared<ae::stream_matrix_bench::P2pBackpressureProducer>(
        fail_ctx, fail_sender, std::move(payload));
    producer->Start(fail_sender.stream_info().max_element_size,
                    [producer, &disc_ok, &disc_done, &disc_done_calls](bool success) {
                      disc_ok = success;
                      disc_done = true;
                      disc_done_calls = producer->done_calls();
                    });
    auto epoch = ae::TimePoint::clock::now();
    for (int tick = 0; tick < 50000 && producer->offset() < 8192; ++tick) {
      fail_env.Update(epoch += std::chrono::milliseconds{1});
    }
    fail_a->SetPermanentLinkError();
    for (int tick = 0; tick < 500000 && !disc_done; ++tick) {
      fail_env.Update(epoch += std::chrono::milliseconds{1});
    }
    Expect(disc_done, "active producer completes on permanent link error");
    Expect(!disc_ok, "active producer reports failure");
    Expect(disc_done_calls == 1, "active producer single on_done");
    Expect(producer->finished(), "active producer finished flag");
  }

  if (g_failures != 0) {
    std::cerr << "p2p_safe_stream_backpressure_ten_transfer_test: " << g_failures
              << " failure(s)\n";
    return 1;
  }

  std::cout << "p2p_safe_stream_backpressure_ten_transfer_test:";
  if (mode != TenTransferRunMode::kDiagnostic) {
    std::cout << " STRICT=" << (strict_ok ? "PASS" : "FAIL") << " complete=" << strict_complete
              << "/10";
  }
  if (mode != TenTransferRunMode::kStrict) {
    std::cout << " DIAGNOSTIC=" << (diagnostic_ok ? "PASS" : "FAIL")
              << " complete=" << diagnostic_complete << "/10";
  }
  std::cout << '\n';
  return 0;
}
