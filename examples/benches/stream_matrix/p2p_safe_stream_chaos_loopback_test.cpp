/*
 * P2pSafeStream loopback with THPT receiver, SizedPacketGate, and controlled loss.
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

struct WireObs {
  std::vector<std::uint16_t> ack_offsets;
  std::vector<std::pair<std::uint16_t, std::size_t>> recv_ranges;
  std::uint64_t max_ack_contiguous_from_zero{0};
};

WireObs g_obs;

void ResetObs() {
  g_obs = {};
}

void UpdateContiguousAck() {
  std::vector<std::uint16_t> sorted = g_obs.ack_offsets;
  std::sort(sorted.begin(), sorted.end());
  std::uint64_t run = 0;
  for (auto off : sorted) {
    if (off + 1 == run || (run == 0 && off == 0)) {
      run = off + 1;
    } else if (off >= run) {
      break;
    }
  }
  g_obs.max_ack_contiguous_from_zero = run;
}

void InstallObs(char side) {
  SetStreamMatrixTraceSide(side);
  SetStreamMatrixBenchTrace(
      nullptr, nullptr,
      [](StreamMatrixSafeStreamAckTrace const& t) {
        g_obs.ack_offsets.push_back(t.ack_offset);
        UpdateContiguousAck();
      },
      [](StreamMatrixSafeStreamRecvTrace const& t) {
        g_obs.recv_ranges.emplace_back(t.begin_offset, t.data_bytes);
      });
}

class ChaosLoopbackP2pTransport final : public ByteStream,
                                        public std::enable_shared_from_this<
                                            ChaosLoopbackP2pTransport> {
 public:
  using DropFn = std::function<bool(int seq, DataBuffer const& buf)>;

  ChaosLoopbackP2pTransport(AeContext const& context, std::size_t max_write_bytes,
                            DropFn drop_fn, bool coalesce)
      : ByteStream{context},
        context_{context},
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
  bool ok{false};
  std::array<WriteAction::Status, 4> write_status{};
  std::size_t rx_bytes{0};
  bool receiver_complete{false};
};

RunResult RunFourThptChaos(ChaosLoopbackP2pTransport::DropFn drop_a,
                           ChaosLoopbackP2pTransport::DropFn drop_b,
                           bool coalesce_flush, int max_ms) {
  RunResult out{};
  ResetObs();
  LoopbackTestContext env;
  AeContext ctx{env};
  auto const inner_max =
      MaxP2pStreamWritePayloadBytesForDatagramBudget(1200);

  auto link_a = std::make_shared<ChaosLoopbackP2pTransport>(ctx, inner_max, drop_a,
                                                             coalesce_flush);
  auto link_b = std::make_shared<ChaosLoopbackP2pTransport>(ctx, inner_max, drop_b,
                                                             coalesce_flush);
  link_a->SetPeer(link_b);
  link_b->SetPeer(link_a);

  InstallObs('A');
  auto config = BenchSafeStreamConfig();
  P2pSafeStream sender{ctx, config, link_a};
  P2pSafeStream receiver{ctx, config, link_b};

  auto blob = stream_matrix_bench::BuildThroughputBlob();
  stream_matrix_bench::StreamingReceiverState thpt_rx(blob, 7,
                                                      stream_matrix_bench::
                                                          kUdpFourBlockTotalBytes);
  MultiSubscription subs;
  subs += receiver.out_data_event().Subscribe([&](DataBuffer const& data) {
    thpt_rx.Feed(data, stream_matrix_bench::BenchInstant{} +
                           std::chrono::milliseconds{1});
  });

  std::array<std::optional<WriteAction::Status>, 4> final_status{};
  for (std::uint64_t i = 0; i < 4; ++i) {
    auto chunk = stream_matrix_bench::MakeThptChunk(
        blob, 7, i, i * 1024, 1024);
    auto& wa = sender.Write(std::move(chunk));
    subs += wa.status_event().Subscribe([&, i](WriteAction::Status status) {
      final_status[i] = status;
    });
  }

  auto epoch = TimePoint::clock::now();
  for (int tick = 0; tick < max_ms; ++tick) {
    env.Update(epoch += std::chrono::milliseconds{1});
    if (coalesce_flush && tick % 5 == 0) {
      link_a->FlushCoalesced();
      link_b->FlushCoalesced();
    }
    bool all_terminal = true;
    for (auto const& st : final_status) {
      if (!st.has_value()) {
        all_terminal = false;
        break;
      }
    }
    if (all_terminal && thpt_rx.Complete()) {
      break;
    }
  }
  link_a->FlushCoalesced();
  link_b->FlushCoalesced();
  env.Update(epoch += std::chrono::milliseconds{1});

  for (std::size_t i = 0; i < 4; ++i) {
    out.write_status[i] = final_status[i].value_or(WriteAction::Status::kFail);
  }
  out.rx_bytes = thpt_rx.bytes_unique();
  out.receiver_complete = thpt_rx.Complete();
  out.ok = out.receiver_complete;
  for (std::size_t i = 0; i < 4; ++i) {
    if (out.write_status[i] == WriteAction::Status::kSuccess && out.rx_bytes <
        stream_matrix_bench::kUdpFourBlockTotalBytes) {
      out.ok = false;
    }
  }
  SetStreamMatrixBenchTrace(nullptr, nullptr, nullptr, nullptr);
  return out;
}

}  // namespace

std::uint64_t ObsMaxAckContiguous() { return g_obs.max_ack_contiguous_from_zero; }

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
    auto r = ae::RunFourThptChaos({}, {}, false, 15000);
    Expect(r.receiver_complete, "no loss: THPT complete");
    Expect(r.rx_bytes == ae::stream_matrix_bench::kUdpFourBlockTotalBytes,
           "no loss: byte count");
    for (int i = 0; i < 4; ++i) {
      Expect(r.write_status[static_cast<std::size_t>(i)] ==
                 ae::WriteAction::Status::kSuccess,
             "no loss: write success");
    }
  }
  {
    int drops_left = 1;
    auto drop_once = [&](int seq, ae::DataBuffer const&) {
      if (drops_left > 0 && seq == 3) {
        --drops_left;
        return true;
      }
      return false;
    };
    auto r = ae::RunFourThptChaos(drop_once, {}, false, 20000);
    Expect(r.receiver_complete, "single drop: recovered");
    Expect(r.rx_bytes == ae::stream_matrix_bench::kUdpFourBlockTotalBytes,
           "single drop: full rx");
  }
  {
    auto r = ae::RunFourThptChaos({}, {}, true, 20000);
    Expect(r.receiver_complete, "coalesced flush: complete");
  }
  {
    int ack_drops = 1;
    auto drop_b = [&](int, ae::DataBuffer const& buf) {
      if (ack_drops > 0 && buf.size() < 96) {
        --ack_drops;
        return true;
      }
      return false;
    };
    auto r = ae::RunFourThptChaos({}, drop_b, false, 25000);
    Expect(r.receiver_complete, "lost ack: eventually complete");
    Expect(ae::ObsMaxAckContiguous() <= 65535, "ack trace recorded");
  }
  if (g_failures != 0) {
    std::cerr << "p2p_safe_stream_chaos_loopback_test: " << g_failures
              << " failure(s)\n";
    return 1;
  }
  std::cout << "p2p_safe_stream_chaos_loopback_test: OK\n";
  return 0;
}
