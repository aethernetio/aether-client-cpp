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

#include "stream_matrix/bench_runner.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>
#include <utility>

#define AE_EXAMPLE_ETHERNET 1

#include "../../common/aether_construct_ethernet.h"

#include "stream_matrix/bench_envelope.h"
#include "stream_matrix/bench_stats.h"

namespace ae::stream_matrix_bench {
Uid const& StreamMatrixBench::ParentUid() {
  static Uid const kUid =
      Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4");
  return kUid;
}

namespace {

Duration MsToDuration(double ms) {
  return std::chrono::duration_cast<Duration>(
      std::chrono::duration<double, std::milli>(ms));
}

}  // namespace

BenchCliOptions ParseArgs(int argc, char* argv[]) {
  BenchCliOptions options;
#ifdef AETHER_STREAM_MATRIX_SDK_SHA
  options.sdk_sha = AETHER_STREAM_MATRIX_SDK_SHA;
#endif
  for (int i = 1; i < argc; ++i) {
    std::string_view arg{argv[i]};
    if (arg == "--smoke") {
      options.smoke = true;
    } else if (arg == "--full") {
      options.full_matrix = true;
    } else if (arg == "--max-rate" && i + 1 < argc) {
      options.max_messages_per_sec = std::stod(argv[++i]);
    } else if (arg == "--warmup-sec" && i + 1 < argc) {
      options.warmup = std::chrono::seconds{std::stoll(argv[++i])};
    } else if (arg == "--measure-sec" && i + 1 < argc) {
      options.measure = std::chrono::seconds{std::stoll(argv[++i])};
    } else if (arg == "--late-sec" && i + 1 < argc) {
      options.late_wait = std::chrono::seconds{std::stoll(argv[++i])};
    } else if (arg == "--output" && i + 1 < argc) {
      options.output_dir = argv[++i];
    } else if (arg == "--sdk-sha" && i + 1 < argc) {
      options.sdk_sha = argv[++i];
    } else if (arg == "--server-version" && i + 1 < argc) {
      options.server_version = argv[++i];
    }
  }
  if (!options.smoke && !options.full_matrix) {
    options.smoke = true;
  }
  return options;
}

StreamMatrixBench::StreamMatrixBench(BenchCliOptions options)
    : options_{std::move(options)} {
  safe_stream_config_ = SafeStreamConfig{
      .window_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
      .max_packet_size = AE_SAFE_STREAM_CAPACITY / 2 - 1,
      .max_repeat_count = 10,
      .wait_ack_timeout = std::chrono::milliseconds{1500},
      .send_ack_timeout = std::chrono::milliseconds{0},
      .send_repeat_timeout = std::chrono::milliseconds{200},
  };
  safe_doc_.capacity = AE_SAFE_STREAM_CAPACITY;
  safe_doc_.config = safe_stream_config_;
}

bool StreamMatrixBench::PumpUntil(std::function<bool()> predicate,
                                  Duration timeout) {
  auto const deadline = Now() + timeout;
  while (!app_->IsExited()) {
    if (predicate()) {
      return true;
    }
    if (Now() >= deadline) {
      return false;
    }
    OnTick(Now());
    auto next_time = app_->Update(Now());
    app_->WaitUntil(std::min(next_time, Now() + std::chrono::milliseconds{5}));
  }
  return false;
}

TransportSelection StreamMatrixBench::DiscoverTransports() {
  TransportSelection selection;
  auto probe = std::make_shared<P2pStream>(*ae_context_, client_a_.Load(),
                                           client_b_->uid(), P2pPortHandle{});
  PumpUntil(
      [&]() {
        for (std::size_t p = 0; p < AE_CLOUD_MAX_SERVER_CONNECTIONS; ++p) {
          auto info = probe->SendPathStreamInfoAtPriority(p);
          if (info.link_state != LinkState::kLinked) {
            continue;
          }
          if (!info.is_reliable && !selection.udp_priority) {
            selection.udp_priority = p;
          }
          if (info.is_reliable && !selection.tcp_priority) {
            selection.tcp_priority = p;
          }
        }
        return selection.udp_priority && selection.tcp_priority;
      },
      std::chrono::seconds{120});
  return selection;
}

bool StreamMatrixBench::SetupMode(BenchStreamKind stream,
                                  BenchTransportKind transport,
                                  TransportSelection const& selection) {
  TeardownStreams();
  write_status_subs_.Reset();
  port_sub_.Reset();
  recv_sub_.Reset();

  auto priority = transport == BenchTransportKind::kUdp
                      ? selection.udp_priority
                      : selection.tcp_priority;
  if (!priority) {
    return false;
  }

  auto policy = RequestPolicy::Priority{*priority};

  using namespace std::chrono_literals;
  client_a_->connectivity_policy()
      ->ConfigureRxTimings(RequestPolicy::All{})
      .ForPriority<0>(RxTimingConf::Every(5s));
  client_b_->connectivity_policy()
      ->ConfigureRxTimings(RequestPolicy::All{})
      .ForPriority<0>(RxTimingConf::Every(5s));

  port_sub_ = client_b_->message_stream_manager().new_port_event().Subscribe(
      [this, stream, policy](P2pPortHandle& port_handle) {
        auto dest = port_handle.destination();
        p2p_b_ = std::make_shared<P2pStream>(*ae_context_, client_b_.Load(), dest,
                                             std::move(port_handle), policy);
        if (stream == BenchStreamKind::kP2pSafeStream) {
          safe_b_ = make_unique<P2pSafeStream>(*ae_context_, safe_stream_config_,
                                              p2p_b_);
          recv_sub_ = safe_b_->out_data_event().Subscribe(
              [this](DataBuffer const& data) { OnReceived(data); });
        } else {
          recv_sub_ = p2p_b_->out_data_event().Subscribe(
              [this](DataBuffer const& data) { OnReceived(data); });
        }
      });

  auto handle = client_a_->message_stream_manager().CreatePort(client_b_->uid());
  p2p_a_ = std::make_shared<P2pStream>(*ae_context_, client_a_.Load(),
                                       client_b_->uid(), std::move(handle),
                                       policy);

  if (!PumpUntil(
          [&]() {
            return p2p_a_->stream_info().link_state == LinkState::kLinked;
          },
          std::chrono::seconds{90})) {
    log_stream_ << "SetupMode: sender link timeout\n";
    return false;
  }

  auto send_info = p2p_a_->stream_info();
  bool want_reliable = transport == BenchTransportKind::kTcp;
  if (send_info.is_reliable != want_reliable) {
    log_stream_ << "TRANSPORT_MISMATCH expected reliable=" << want_reliable
                << " got=" << send_info.is_reliable << '\n';
    return false;
  }

  if (stream == BenchStreamKind::kP2pSafeStream) {
    safe_a_ = make_unique<P2pSafeStream>(*ae_context_, safe_stream_config_,
                                         p2p_a_);
    active_stream_ = safe_a_.get();
    safe_doc_.max_element_size_reported = safe_a_->stream_info().max_element_size;
    safe_doc_.rec_element_size_reported = safe_a_->stream_info().rec_element_size;
  } else {
    active_stream_ = p2p_a_.get();
  }

  if (!PumpUntil(
          [&]() {
            return active_stream_->stream_info().link_state == LinkState::kLinked;
          },
          std::chrono::seconds{30})) {
    log_stream_ << "SetupMode: active stream link timeout\n";
    return false;
  }

  if (!PumpUntil([&]() { return p2p_b_ != nullptr; }, std::chrono::seconds{90})) {
    log_stream_ << "SetupMode: receiver port timeout\n";
    return false;
  }
  return true;
}

void StreamMatrixBench::TeardownStreams() {
  port_sub_.Reset();
  recv_sub_.Reset();
  safe_a_.reset();
  safe_b_.reset();
  p2p_a_.reset();
  p2p_b_.reset();
  active_stream_ = nullptr;
}

void StreamMatrixBench::OnReceived(DataBuffer const& data) {
  if (!point_active_) {
    return;
  }
  auto envelope = DeserializeEnvelope(data);
  if (!envelope) {
    return;
  }
  point_stats_.NoteReceived(envelope->sequence, Now(),
                            envelope->payload.size());
}

void StreamMatrixBench::OnTick(TimePoint now) {
  if (!point_active_ || !active_stream_) {
    return;
  }

  if (now >= deadline_) {
    point_active_ = false;
    return;
  }

  while (now >= next_scheduled_send_ && now < deadline_) {
    point_stats_.NoteScheduled();
    bool in_measure = (now >= measure_begin_ && now < measure_end_);
    auto const sequence = next_sequence_++;

    BenchEnvelope envelope{.sequence = sequence, .payload = payload_pattern_};
    auto wire = SerializeEnvelope(envelope);
    auto send_time = Now();

    auto& wa = active_stream_->Write(std::move(wire));
    if (wa.is_finished()) {
      point_stats_.NoteSkippedBackpressure();
    } else {
      point_stats_.NoteWriteCalled(sequence, send_time, next_scheduled_send_,
                                   in_measure);
      write_status_subs_ += wa.status_event().Subscribe(
          [this](WriteAction::Status status) {
            if (status == WriteAction::Status::kSuccess) {
              point_stats_.NoteWriteOk();
            } else if (status == WriteAction::Status::kFail) {
              point_stats_.NoteWriteFailed();
            } else if (status == WriteAction::Status::kStop) {
              point_stats_.NoteWriteStopped();
            }
          });
    }

    next_scheduled_send_ += MsToDuration(period_ms_);
    now = Now();
  }
}

std::optional<CsvRow> StreamMatrixBench::RunPoint(
    BenchStreamKind stream, BenchTransportKind transport,
    std::size_t payload_bytes, double period_ms) {
  payload_bytes_ = payload_bytes;
  period_ms_ = period_ms;

  payload_pattern_.assign(payload_bytes, 0);
  for (std::size_t i = 0; i < payload_bytes; ++i) {
    payload_pattern_[i] = static_cast<std::uint8_t>(i & 0xFFU);
  }
  serialized_payload_ = SerializeEnvelope(
      BenchEnvelope{.sequence = 0, .payload = payload_pattern_});

  auto limit = ComputePayloadLimit(
      stream, transport, active_stream_->stream_info(), safe_doc_,
      MeasureEnvelopeOverheadBytes());
  if (payload_bytes > limit.max_payload_bytes) {
    CsvRow row;
    row.transport = std::string{TransportKindName(transport)};
    row.stream = std::string{StreamKindName(stream)};
    row.payload_bytes = payload_bytes;
    row.period_ms = period_ms;
    row.sdk_sha = options_.sdk_sha;
    row.server_version = options_.server_version;
    row.outcome = "SKIP_SIZE_LIMIT";
    row.max_udp_payload_bytes = kBenchmarkUdpPayloadBudgetBytes;
    return row;
  }

  write_status_subs_.Reset();

  point_begin_ = Now();
  measure_begin_ = point_begin_ + options_.warmup;
  measure_end_ = measure_begin_ + options_.measure;
  deadline_ = measure_end_ + options_.late_wait;
  point_stats_.ResetMeasureWindow(measure_begin_, measure_end_);
  next_scheduled_send_ = point_begin_;
  point_active_ = true;

  PumpUntil(
      [&]() { return Now() >= deadline_; },
      options_.warmup + options_.measure + options_.late_wait +
          std::chrono::seconds{5});
  point_active_ = false;

  point_stats_.FinalizeAfterDeadline(deadline_);

  CsvRow row;
  row.transport = std::string{TransportKindName(transport)};
  row.stream = std::string{StreamKindName(stream)};
  row.payload_bytes = payload_bytes;
  row.serialized_bytes = serialized_payload_.size();
  row.max_udp_payload_bytes = kBenchmarkUdpPayloadBudgetBytes;
  row.period_ms = period_ms;
  row.counters = point_stats_.counters();
  row.latency = point_stats_.Latency();
  row.send_rate = point_stats_.SendRate();
  row.receive_rate = point_stats_.ReceiveRate();
  row.goodput_bytes_s = point_stats_.GoodputBytesPerSec();
  if (row.counters.write_calls > 0) {
    row.delivery_loss_pct =
        100.0 * static_cast<double>(row.counters.missing_after_deadline) /
        static_cast<double>(row.counters.write_calls);
  }
  row.sdk_sha = options_.sdk_sha;
  row.server_version = options_.server_version;
  if (row.counters.rx_unique == 0 && row.counters.write_ok > 0) {
    row.outcome = "RX_MISSING";
  } else {
    row.outcome = "OK";
  }
  return row;
}

int StreamMatrixBench::Run() {
  std::filesystem::create_directories(options_.output_dir);
  csv_stream_.open(options_.output_dir / "results.csv",
                   std::ios::out | std::ios::trunc);
  log_stream_.open(options_.output_dir / "run.log",
                   std::ios::out | std::ios::trunc);
  WriteCsvHeader(csv_stream_);

  app_ = examples::construct_aether_app();
  ae_context_.emplace(*app_);

  app_->aether()->SelectClient(ParentUid(), "sender").result_event().Subscribe(
      [&](auto const& res) {
        if (res) {
          client_a_ = res.value();
        }
      });
  app_->aether()->SelectClient(ParentUid(), "receiver").result_event().Subscribe(
      [&](auto const& res) {
        if (res) {
          client_b_ = res.value();
        }
      });

  if (!PumpUntil([&]() { return client_a_ && client_b_; },
                 std::chrono::seconds{180})) {
    log_stream_ << "Failed to acquire clients\n";
    return 1;
  }

  {
    auto settle_until = Now() + std::chrono::seconds{6};
    while (Now() < settle_until && !app_->IsExited()) {
      auto next_time = app_->Update(Now());
      app_->WaitUntil(std::min(next_time, Now() + std::chrono::milliseconds{5}));
    }
    client_a_->cloud_connection().Restream();
    client_b_->cloud_connection().Restream();
  }

  auto transports = DiscoverTransports();
  log_stream_ << "UDP priority: "
              << (transports.udp_priority
                      ? std::to_string(*transports.udp_priority)
                      : "UNAVAILABLE")
              << " TCP priority: "
              << (transports.tcp_priority
                      ? std::to_string(*transports.tcp_priority)
                      : "UNAVAILABLE")
              << '\n';

  struct Mode {
    BenchStreamKind stream;
    BenchTransportKind transport;
  };
  std::vector<Mode> modes{
      {BenchStreamKind::kP2pStream, BenchTransportKind::kUdp},
      {BenchStreamKind::kP2pStream, BenchTransportKind::kTcp},
      {BenchStreamKind::kP2pSafeStream, BenchTransportKind::kUdp},
      {BenchStreamKind::kP2pSafeStream, BenchTransportKind::kTcp},
  };

  std::vector<std::size_t> sizes =
      options_.smoke ? std::vector<std::size_t>{100} : DefaultPayloadSweep();
  auto periods = options_.smoke
                     ? std::vector<double>{1000.0}
                     : PeriodsMsForMaxRate(options_.max_messages_per_sec);

  for (auto const& mode : modes) {
    if (!SetupMode(mode.stream, mode.transport, transports)) {
      CsvRow row;
      row.transport = std::string{TransportKindName(mode.transport)};
      row.stream = std::string{StreamKindName(mode.stream)};
      row.outcome = "TRANSPORT_UNAVAILABLE";
      row.sdk_sha = options_.sdk_sha;
      row.server_version = options_.server_version;
      WriteCsvRow(csv_stream_, row);
      continue;
    }

    for (double period : periods) {
      for (auto size : sizes) {
        auto row = RunPoint(mode.stream, mode.transport, size, period);
        if (row) {
          WriteCsvRow(csv_stream_, *row);
          csv_stream_.flush();
        }
        TeardownStreams();
        if (!SetupMode(mode.stream, mode.transport, transports)) {
          break;
        }
      }
    }
    TeardownStreams();
  }

  log_stream_ << "Finished\n";
  app_->Exit(0);
  return 0;
}

}  // namespace ae::stream_matrix_bench
