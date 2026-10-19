/*
 * P2pSafeStream loopback: THPT receiver, SizedPacketGate, loss/ACK checks.
 */

#include <algorithm>
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
#include "aether/stream_matrix_bench_trace.h"
#include "aether/write_action/failed_write_action.h"
#include "aether/write_action/write_action.h"

#include "stream_matrix/bench_clock.h"
#include "stream_matrix/bench_streaming_receiver.h"
#include "stream_matrix/bench_throughput_frame.h"

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
    context.scheduler().Task([&]() { SetStatus(Status::kSuccess); });
  }
};

constexpr char kThptReceiverLeg = 'B';

struct LegacyAckAtTimeTracker {
  void AddRecv(std::uint16_t begin, std::uint16_t delta, std::size_t len, bool reset) {
    if (reset) {
      ranges_.clear();
    }
    if (len == 0) {
      return;
    }
    std::uint32_t const start =
        static_cast<std::uint32_t>(begin) + static_cast<std::uint32_t>(delta);
    ranges_.push_back(
        {start, start + static_cast<std::uint32_t>(len)});
    std::sort(ranges_.begin(), ranges_.end(),
              [](auto const& a, auto const& b) { return a.start < b.start; });
    std::vector<Span> merged;
    for (auto const& r : ranges_) {
      if (merged.empty() || merged.back().end < r.start) {
        merged.push_back(r);
      } else {
        merged.back().end = std::max(merged.back().end, r.end);
      }
    }
    ranges_ = std::move(merged);
  }

  bool Through(std::uint16_t ack) const {
    if (ranges_.empty()) {
      return false;
    }
    std::uint32_t covered = ranges_.front().start;
    for (auto const& r : ranges_) {
      if (r.start > covered) {
        break;
      }
      covered = std::max(covered, r.end);
    }
    return covered >= static_cast<std::uint32_t>(ack) + 1;
  }

 private:
  struct Span {
    std::uint32_t start{};
    std::uint32_t end{};
  };
  std::vector<Span> ranges_;
};

struct Obs {
  struct TraceEvent {
    enum class Kind { kRecv, kAckTx } kind{};
    StreamMatrixSafeStreamRecvTrace recv{};
    StreamMatrixSafeStreamAckTrace ack{};
  };

  std::vector<TraceEvent> events;
  std::vector<StreamMatrixSafeStreamAckTrace> acks;
  int drop_count{0};

  bool ReplayAckCoverage() const {
    LegacyAckAtTimeTracker legacy;
    bool saw_tx{false};
    bool legacy_ok = true;
    for (auto const& ev : events) {
      if (ev.kind == TraceEvent::Kind::kRecv) {
        auto const& t = ev.recv;
        legacy.AddRecv(t.begin_offset, t.delta_offset, t.data_bytes, t.reset);
      } else if (ev.ack.side == kThptReceiverLeg) {
        saw_tx = true;
        if (!legacy.Through(ev.ack.ack_offset)) {
          legacy_ok = false;
        }
      }
    }
    return legacy_ok && saw_tx;
  }
};

Obs g_obs;

void ResetObs() {
  g_obs = {};
}

void InstallObs() {
  SetStreamMatrixBenchTrace(
      nullptr, nullptr,
      [](StreamMatrixSafeStreamAckTrace const& t) {
        g_obs.acks.push_back(t);
        if (t.role == StreamMatrixSafeStreamAckRole::kTx) {
          g_obs.events.push_back(
              Obs::TraceEvent{.kind = Obs::TraceEvent::Kind::kAckTx, .ack = t});
        }
      },
      [](StreamMatrixSafeStreamRecvTrace const& t) {
        if (t.side != kThptReceiverLeg || t.data_bytes == 0) {
          return;
        }
        g_obs.events.push_back(
            Obs::TraceEvent{.kind = Obs::TraceEvent::Kind::kRecv, .recv = t});
      });
}

class ChaosLoopbackP2pTransport final : public ByteStream,
                                        public std::enable_shared_from_this<
                                            ChaosLoopbackP2pTransport> {
 public:
  using DropFn = std::function<bool(int seq, DataBuffer const& buf)>;

  ChaosLoopbackP2pTransport(AeContext const& context, std::size_t max_write_bytes,
                            char leg_side, DropFn drop_fn, bool coalesce)
      : ByteStream{context},
        context_{context},
        leg_side_{leg_side},
        drop_fn_{std::move(drop_fn)},
        coalesce_deliver_{coalesce},
        stream_info_{max_write_bytes, max_write_bytes, false, LinkState::kLinked,
                     true},
        out_data_event_{context},
        stream_update_event_{context} {}

  void SetPeer(std::weak_ptr<ChaosLoopbackP2pTransport> peer) {
    peer_ = std::move(peer);
  }

  WriteAction& Write(DataBuffer&& buffer) override {
    ++wire_seq_;
    if (drop_fn_ && drop_fn_(wire_seq_, buffer)) {
      ++g_obs.drop_count;
      if (!ok_action_ || ok_action_->is_finished()) {
        ok_action_.emplace(context_);
      }
      return *ok_action_;
    }
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
    ++wire_seq_;
    if (drop_fn_ && drop_fn_(wire_seq_, merged)) {
      ++g_obs.drop_count;
      return;
    }
    if (auto peer = peer_.lock()) {
      peer->Deliver(std::move(merged));
    }
  }

  StreamInfo stream_info() const override { return stream_info_; }
  StreamUpdateEvent const& stream_update_event() override {
    return stream_update_event_;
  }
  OutDataEvent const& out_data_event() override { return out_data_event_; }

  void Deliver(DataBuffer buffer) {
    SetStreamMatrixTraceSide(leg_side_);
    out_data_event_.Emit(std::move(buffer));
  }

 private:
  AeContext context_;
  char leg_side_;
  DropFn drop_fn_;
  bool coalesce_deliver_{false};
  StreamInfo stream_info_;
  OutDataEvent out_data_event_;
  StreamUpdateEvent stream_update_event_;
  std::weak_ptr<ChaosLoopbackP2pTransport> peer_;
  std::deque<DataBuffer> pending_;
  std::optional<MockStreamWriteAction> ok_action_;
  int wire_seq_{0};
};

SafeStreamConfig BenchSafeStreamConfig() {
  return SafeStreamConfig{
      .window_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
      .max_packet_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
      .max_repeat_count = 10,
      .wait_ack_timeout = std::chrono::milliseconds{1500},
      .send_ack_timeout = std::chrono::milliseconds{0},
      .send_repeat_timeout = std::chrono::milliseconds{50},
  };
}

struct RunResult {
  bool thpt_ok{false};
  bool integrity_ok{false};
  bool acks_ok{false};
  std::array<std::optional<WriteAction::Status>, 4> write_status{};
  std::array<bool, 4> write_range_complete{};
  std::size_t rx_bytes{0};
  int drops{0};
};

RunResult RunFourThptChaos(ChaosLoopbackP2pTransport::DropFn drop_a,
                           ChaosLoopbackP2pTransport::DropFn drop_b,
                           bool coalesce_flush, int max_ms) {
  RunResult out{};
  ResetObs();
  InstallObs();
  LoopbackTestContext env;
  AeContext ctx{env};
  auto const inner_max =
      MaxP2pStreamWritePayloadBytesForDatagramBudget(1200);

  auto link_a = std::make_shared<ChaosLoopbackP2pTransport>(ctx, inner_max, 'A',
                                                             drop_a, coalesce_flush);
  auto link_b = std::make_shared<ChaosLoopbackP2pTransport>(ctx, inner_max, 'B',
                                                             drop_b, coalesce_flush);
  link_a->SetPeer(link_b);
  link_b->SetPeer(link_a);

  auto config = BenchSafeStreamConfig();
  P2pSafeStream sender{ctx, config, link_a};
  P2pSafeStream receiver{ctx, config, link_b};

  auto blob = stream_matrix_bench::BuildThroughputBlob();
  stream_matrix_bench::StreamingReceiverState thpt_rx(
      blob, 7, stream_matrix_bench::kUdpFourBlockTotalBytes);
  MultiSubscription subs;
  subs += receiver.out_data_event().Subscribe([&](DataBuffer const& data) {
    thpt_rx.Feed(data, stream_matrix_bench::BenchInstant{} +
                           std::chrono::milliseconds{1});
  });

  for (std::uint64_t i = 0; i < 4; ++i) {
    SetStreamMatrixTraceSide('A');
    auto chunk = stream_matrix_bench::MakeThptChunk(blob, 7, i, i * 1024, 1024);
    auto& wa = sender.Write(std::move(chunk));
    subs += wa.status_event().Subscribe([&, i](WriteAction::Status status) {
      out.write_status[i] = status;
    });
  }

  auto epoch = TimePoint::clock::now();
  for (int tick = 0; tick < max_ms; ++tick) {
    env.Update(epoch += std::chrono::milliseconds{1});
    if (coalesce_flush && tick % 5 == 0) {
      link_a->FlushCoalesced();
      link_b->FlushCoalesced();
    }
  }
  link_a->FlushCoalesced();
  link_b->FlushCoalesced();
  env.Update(epoch += std::chrono::milliseconds{1});

  out.rx_bytes = thpt_rx.bytes_unique();
  out.thpt_ok = thpt_rx.Complete() && !thpt_rx.integrity_fail();
  out.integrity_ok = out.thpt_ok;
  out.acks_ok = g_obs.ReplayAckCoverage();
  out.drops = g_obs.drop_count;
  for (std::size_t i = 0; i < 4; ++i) {
    out.write_range_complete[i] =
        thpt_rx.HasBytes(i * 1024, 1024);
  }
  SetStreamMatrixBenchTrace(nullptr, nullptr, nullptr, nullptr);
  return out;
}

}  // namespace

using DropFn = std::function<bool(int, DataBuffer const&)>;

RunResult RunFourThptChaosPublic(DropFn drop_a, DropFn drop_b, bool coalesce_flush,
                                 int max_ms) {
  return RunFourThptChaos(std::move(drop_a), std::move(drop_b), coalesce_flush,
                          max_ms);
}

bool ExpectSuccessWrites(RunResult const& r) {
  for (auto const& st : r.write_status) {
    if (!st.has_value() || *st != WriteAction::Status::kSuccess) {
      return false;
    }
  }
  return true;
}

bool WritesMatchReceivedBytes(RunResult const& r) {
  for (std::size_t i = 0; i < r.write_status.size(); ++i) {
    if (!r.write_status[i].has_value()) {
      continue;
    }
    if (*r.write_status[i] == WriteAction::Status::kSuccess &&
        !r.write_range_complete[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace ae

static int g_failures = 0;

static void Expect(bool ok, char const* msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << '\n';
    ++g_failures;
  }
}

int main() {
  {
    auto r = ae::RunFourThptChaosPublic({}, {}, false, 15000);
    Expect(r.thpt_ok && r.integrity_ok, "no loss: THPT complete");
    Expect(r.rx_bytes == ae::stream_matrix_bench::kUdpFourBlockTotalBytes,
           "no loss: bytes");
    Expect(ae::ExpectSuccessWrites(r), "no loss: four kSuccess");
    Expect(r.acks_ok, "no loss: ack_tx valid at emission");
    Expect(ae::WritesMatchReceivedBytes(r), "no loss: writes match THPT bytes");
  }
  {
    int drops_left = 1;
    int target = 0;
    auto drop_fragment = [&](int seq, ae::DataBuffer const&) {
      if (drops_left > 0 && seq == 3) {
        --drops_left;
        ++target;
        return true;
      }
      return false;
    };
    auto r = ae::RunFourThptChaosPublic(drop_fragment, {}, false, 25000);
    Expect(r.drops == 1, "fragment drop happened once");
    Expect(r.thpt_ok, "fragment drop: recovered THPT");
    Expect(ae::ExpectSuccessWrites(r), "fragment drop: four kSuccess");
    Expect(ae::WritesMatchReceivedBytes(r), "fragment drop: writes match THPT");
    Expect(r.acks_ok, "fragment drop: acks valid at emission");
  }
  {
    auto drop_coalesced = [&](int, ae::DataBuffer const& buf) {
      return buf.size() > 1500;
    };
    auto r = ae::RunFourThptChaosPublic(drop_coalesced, {}, true, 30000);
    Expect(r.drops >= 1, "coalesced datagram drop injected");
    if (r.thpt_ok) {
      Expect(ae::ExpectSuccessWrites(r), "coalesced drop: four kSuccess");
      Expect(ae::WritesMatchReceivedBytes(r), "coalesced drop: writes match THPT");
      Expect(r.acks_ok, "coalesced drop: acks valid at emission");
    }
  }
  {
    int ack_drops = 0;
    auto drop_b = [&](int, ae::DataBuffer const& buf) {
      if (buf.size() < 96 && ack_drops < 1) {
        ++ack_drops;
        return true;
      }
      return false;
    };
    auto r = ae::RunFourThptChaosPublic({}, drop_b, false, 30000);
    Expect(r.drops >= 1, "ack drop injected");
    Expect(r.thpt_ok, "lost ack: still completes THPT");
    Expect(ae::ExpectSuccessWrites(r), "lost ack: four kSuccess");
    Expect(ae::WritesMatchReceivedBytes(r), "lost ack: writes match THPT");
    Expect(r.acks_ok, "lost ack: acks valid at emission");
  }
  {
    int block_after = 2;
    auto block_mid = [&](int seq, ae::DataBuffer const&) {
      if (seq <= block_after) {
        return false;
      }
      return true;
    };
    auto r = ae::RunFourThptChaosPublic(block_mid, {}, false, 4000);
    Expect(r.drops > 0, "sustained drop after handshake");
    Expect(!r.thpt_ok, "sustained loss: incomplete THPT");
    Expect(ae::WritesMatchReceivedBytes(r),
           "sustained loss: no kSuccess without full write range");
  }
  if (g_failures != 0) {
    std::cerr << "p2p_safe_stream_chaos_loopback_test: " << g_failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "p2p_safe_stream_chaos_loopback_test: OK\n";
  return 0;
}
