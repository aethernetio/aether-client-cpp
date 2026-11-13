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

#include "aether/adaptive_datagram/adaptive_datagram_stream.h"

#include <algorithm>
#include <bitset>
#include <chrono>
#include <deque>
#include <limits>
#include <list>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>

#include "aether/adaptive_datagram/adaptive_datagram_protocol.h"

namespace ae {
namespace adaptive_datagram_stream_internal {
using adaptive_datagram::Datagram;
using adaptive_datagram::Feedback;
using adaptive_datagram::IAdaptiveDatagramApi;

std::uint64_t Micros(TimePoint time) {
  auto const us = std::chrono::duration_cast<std::chrono::microseconds>(
                      time.time_since_epoch())
                      .count();
  return static_cast<std::uint64_t>(std::max<std::int64_t>(0, us));
}

Duration Elapsed(TimePoint now, TimePoint before) {
  auto const us =
      std::chrono::duration_cast<std::chrono::microseconds>(now - before)
          .count();
  return Duration{static_cast<Duration::rep>(std::clamp<std::int64_t>(
      us, 0, std::numeric_limits<Duration::rep>::max()))};
}

std::int64_t LocalPeerTime(std::uint64_t remote, std::int64_t offset) {
  constexpr auto kMax = std::numeric_limits<std::int64_t>::max();
  auto const value = static_cast<std::int64_t>(
      std::min(remote, static_cast<std::uint64_t>(kMax)));
  if (offset > 0 && value > kMax - offset) {
    return kMax;
  }
  return value + offset;
}

std::int64_t Difference(std::int64_t left, std::int64_t right) {
  constexpr auto kMax = std::numeric_limits<std::int64_t>::max();
  constexpr auto kMin = std::numeric_limits<std::int64_t>::min();
  if (right < 0 && left > kMax + right) {
    return kMax;
  }
  if (right > 0 && left < kMin + right) {
    return kMin;
  }
  return left - right;
}

class ApiSender final : public IAdaptiveDatagramApi {
 public:
  void Data(Datagram datagram) override {
    ClientMethod<&IAdaptiveDatagramApi::Data>(std::move(datagram));
  }
  void Report(Feedback feedback) override {
    ClientMethod<&IAdaptiveDatagramApi::Report>(std::move(feedback));
  }
};

class PendingWrite final : public WriteAction {
 public:
  PendingWrite(AeContext const& context, Datagram packet, TimePoint deadline)
      : WriteAction{context},
        packet{std::move(packet)},
        message_sequence{this->packet.sequence},
        deadline{deadline} {}
  void Stop() noexcept override { cancelled = true; }
  void Complete(Status status) {
    if (!is_finished() && !completing) {
      completing = true;
      SetStatus(status);
    }
  }
  Datagram packet;
  std::uint64_t const message_sequence;
  TimePoint deadline;
  bool submitted{false};
  bool cancelled{false};
  bool completing{false};
};

class RejectedWrite final : public WriteAction {
 public:
  RejectedWrite(AeContext const& context, std::weak_ptr<void> owner)
      : WriteAction{context} {
    task = context.scheduler().Task([this, owner = std::move(owner)] {
      auto const keep_alive = owner.lock();
      if (!keep_alive) {
        return;
      }
      task.Reset();
      SetStatus(Status::kFail);
    });
  }

 private:
  TaskSubscription task;
};
}  // namespace adaptive_datagram_stream_internal

struct AdaptiveDatagramStream::Impl : std::enable_shared_from_this<Impl> {
  using PendingWrite = adaptive_datagram_stream_internal::PendingWrite;
  using Datagram = adaptive_datagram::Datagram;
  using Feedback = adaptive_datagram::Feedback;
  using Status = adaptive_datagram::DeliveryStatus;
  using Result = adaptive_datagram::DeliveryResult;

  struct ApiReceiver final : adaptive_datagram::IAdaptiveDatagramApi {
    explicit ApiReceiver(Impl& impl) : impl{&impl} {}
    void Data(Datagram packet) override { impl->Receive(std::move(packet)); }
    void Report(Feedback feedback) override { impl->Receive(feedback); }
    Impl* impl;
  };
  struct Flight {
    std::uint64_t sequence;
    std::uint64_t message_sequence;
    std::size_t size;
    TimePoint mature_at;
    TimePoint sent_at;
    bool received{false};
    bool on_time{false};
    bool confirmed_missing{false};
    Duration latency{0};
    Duration path_latency{0};
    std::size_t wire_bytes{0};
  };

  Impl(AeContext const& context, adaptive_datagram::Config config, Clock clock)
      : context{context},
        config{config},
        clock{clock ? std::move(clock) : Clock{[] { return Now(); }}},
        controller{config.Validate() ? config : adaptive_datagram::Config{},
                   this->clock()},
        protocol{context},
        receiver{*this},
        updated{context},
        output{context},
        delivered{context} {
    valid = config.Validate() &&
            config.max_in_flight_packets <= adaptive_datagram::kReceiptBits &&
            config.max_datagram_size >= 256;
  }

  ~Impl() { Unlink(false); }

  template <typename Packet>
  DataBuffer Pack(Packet const& packet) {
    auto api = ApiContext<adaptive_datagram_stream_internal::ApiSender>{sender};
    if constexpr (std::is_same_v<Packet, Datagram>) {
      api->Data(packet);
    } else {
      api->Report(packet);
    }
    return DataBuffer{std::move(api)};
  }

  StreamInfo Info() const {
    auto info = lower != nullptr ? lower->stream_info() : StreamInfo{};
    info.is_reliable = false;
    // Conservative allowance for protocol IDs, lengths and timestamps. The
    // actual serialized size is checked again before every lower Write.
    auto const limit =
        std::min(info.max_element_size, config.max_datagram_size);
    info.max_element_size = limit > 96 ? limit - 96 : 0;
    info.rec_element_size = info.max_element_size;
    info.is_writable = valid && info.is_writable && info.max_element_size > 0 &&
                       Queued() < config.max_queue_packets;
    return info;
  }

  void ResetPath() {
    path_started_at_ = clock();
    controller.ResetPath(path_started_at_);
    latest_path_latency = Duration{0};
    last_live_sample = 0;
    delivery_measurement_at_.reset();
    peer_measurement_at_ = 0;
    acknowledged_wire_bytes_ = 0;
  }

  void Link(ByteIStream& stream) {
    auto const epoch = link_epoch;
    Unlink();
    if (link_epoch != epoch + 1) {
      return;
    }
    lower = &stream;
    data_sub = lower->out_data_event().Subscribe([this](
                                                     DataBuffer const& data) {
      auto const keep_alive = shared_from_this();
      if (data.empty() || data.size() > config.max_datagram_size) {
        ++stats.malformed;
        return;
      }
      // ApiParser assumes complete method bodies in Debug. Preflight bounded
      // datagrams through the non-asserting reader before invoking dispatch.
      auto archive = seri::BinaryArchive<MessageBuffer>{
          MessageBuffer{const_cast<DataBuffer&>(data)},  // NOLINT(*const-cast*)
          data.size()};
      MessageId id{};
      if (archive.Load(id).IsErr()) {
        ++stats.malformed;
        return;
      }
      bool complete = false;
      if (id == 3) {
        Datagram packet;
        complete = !archive.Load(packet).IsErr();
      } else if (id == 4) {
        Feedback packet;
        complete = !archive.Load(packet).IsErr();
      }
      if (!complete || archive.buffer().read_offset != data.size()) {
        ++stats.malformed;
        return;
      }
      auto parser = ApiParser{protocol, data};
      if (!parser.Parse(receiver)) {
        ++stats.malformed;
      }
    });
    update_sub = lower->stream_update_event().Subscribe([this] {
      auto const keep_alive = shared_from_this();
      ResetPath();
      updated.Emit();
      Wake(clock());
    });
    ResetPath();
    next_send = clock();
    Wake(clock());
    updated.Emit();
  }

  void Unlink(bool notify = true) {
    auto const epoch = ++link_epoch;
    task.Reset();
    data_sub.Reset();
    update_sub.Reset();
    write_sub.Reset();
    auto* old_active = active;
    active = nullptr;
    active_pending = nullptr;
    lower = nullptr;
    history.clear();
    flights.clear();
    // Keep the deduplication/session history across carrier relinks. Changing
    // the peer session requires a new stream instance with a new local ID.
    if (old_active != nullptr && !old_active->is_finished()) {
      old_active->Stop();
      if (epoch != link_epoch) {
        return;
      }
    }
    // Cancel pending application writes asynchronously while the stream lives.
    // Destruction simply releases them, as with the other runtime streams.
    for (auto& write : pending) {
      write->cancelled = true;
      if (notify) {
        write->Complete(WriteAction::Status::kStop);
      }
      if (epoch != link_epoch) {
        return;
      }
    }
  }

  std::size_t Queued() const {
    return static_cast<std::size_t>(std::count_if(
        pending.begin(), pending.end(),
        [](auto const& w) { return !w->submitted && !w->is_finished(); }));
  }

  WriteAction& Write(DataBuffer&& data) {
    auto const info = Info();
    if (!info.is_writable || data.empty() ||
        data.size() > info.max_element_size ||
        sequence == std::numeric_limits<std::uint64_t>::max()) {
      ++stats.rejected;
      if (!failed || failed->is_finished()) {
        failed.emplace(context, weak_from_this());
      }
      return *failed;
    }
    auto const now = clock();
    Datagram packet{config.session_id,
                    config.stream_id,
                    ++sequence,
                    adaptive_datagram_stream_internal::Micros(now),
                    0,
                    config.latency_target.count(),
                    std::move(data)};
    pending.push_back(std::make_unique<PendingWrite>(
        context, std::move(packet), now + config.latency_target));
    ++stats.accepted;
    Wake(now);
    return *pending.back();
  }

  void Wake(TimePoint at) {
    if (!valid || lower == nullptr || (task && at >= scheduled_at)) {
      return;
    }
    task.Reset();
    scheduled_at = at;
    task = context.scheduler().DelayedTask(
        [weak = weak_from_this()] {
          if (auto self = weak.lock()) {
            self->task.Reset();
            self->Tick();
          }
        },
        at);
    assert(task && "Adaptive datagram task allocation failed");
  }

  void Outcome(std::uint64_t seq, std::size_t bytes, Status status,
               Duration latency, TimePoint now,
               Duration path_latency = Duration{0},
               std::optional<TimePoint> sent_at = std::nullopt) {
    // Producer overflow is reported separately: reducing the network rate in
    // response to producer queue expiry would make that same queue grow faster.
    if (status == Status::kDelivered || status == Status::kLost ||
        status == Status::kLate) {
      controller.RecordOutcome(now, bytes, status == Status::kDelivered,
                               path_latency, sent_at);
    } else if (status == Status::kUnconfirmed) {
      controller.RecordUnconfirmed(now);
    }
    switch (status) {
      case Status::kDelivered:
        ++stats.delivered;
        break;
      case Status::kLost:
        ++stats.lost;
        break;
      case Status::kUnconfirmed:
        ++stats.unconfirmed;
        break;
      case Status::kLate:
        ++stats.late;
        break;
      case Status::kExpired:
        ++stats.expired;
        break;
      case Status::kCancelled:
        ++stats.cancelled;
        break;
      case Status::kLocalFailure:
        ++stats.local_failures;
        break;
    }
    delivered.Emit(Result{seq, status, bytes, latency});
  }

  void Submit(DataBuffer wire, PendingWrite* write, TimePoint now) {
    auto const epoch = link_epoch;
    auto const bytes = wire.size();
    if (bytes > config.max_datagram_size ||
        bytes > lower->stream_info().max_element_size) {
      if (write != nullptr) {
        auto const seq = write->packet.sequence;
        auto it =
            std::find_if(history.begin(), history.end(),
                         [seq](auto const& f) { return f.sequence == seq; });
        if (it != history.end()) {
          auto const f = *it;
          flights.erase(it->sequence);
          history.erase(it);
          Outcome(f.message_sequence, f.size, Status::kLocalFailure,
                  Duration{0}, now);
          if (epoch != link_epoch) {
            return;
          }
        }
        write->Complete(WriteAction::Status::kFail);
      }
      ++stats.malformed;
      return;
    }
    auto const rate = controller.rate_bytes_per_second();
    if (write != nullptr && !history.empty()) {
      history.back().wire_bytes = bytes;
    }
    auto const spacing = (bytes * std::uint64_t{1000000} + rate - 1) / rate;
    next_send = now + std::chrono::microseconds{static_cast<std::int64_t>(
                          std::max<std::uint64_t>(1, spacing))};
    active_pending = write;
    auto const observe = write != nullptr && config.on_local_write_complete;
    auto const observed_message_sequence =
        observe ? write->message_sequence : 0;
    auto const queued_at =
        observe ? write->deadline - config.latency_target : TimePoint{};
    auto const submitted_at = observe ? clock() : TimePoint{};
    auto& action = lower->Write(std::move(wire));
    active = &action;
    write_sub = action.status_event().Subscribe(
        [this, bytes, observe, observed_message_sequence, queued_at,
         submitted_at](WriteAction::Status s) {
          auto const completed_at = observe ? clock() : TimePoint{};
          auto const keep_alive = shared_from_this();
          auto const epoch = link_epoch;
          auto* pending_write = active_pending;
          active = nullptr;
          active_pending = nullptr;
          write_sub.Reset();
          if (s == WriteAction::Status::kSuccess) {
            stats.wire_bytes += bytes;
            if (pending_write != nullptr) {
              ++stats.sent;
            } else {
              ++stats.feedback_sent;
            }
          }
          if (pending_write != nullptr) {
            if (s != WriteAction::Status::kSuccess) {
              auto const seq = pending_write->packet.sequence;
              auto it = std::find_if(
                  history.begin(), history.end(),
                  [seq](auto const& f) { return f.sequence == seq; });
              if (it != history.end()) {
                auto const size = it->size;
                auto const message_sequence = it->message_sequence;
                flights.erase(it->sequence);
                history.erase(it);
                auto const now = clock();
                auto const status = pending_write->cancelled
                                        ? Status::kCancelled
                                        : (now >= pending_write->deadline
                                               ? Status::kExpired
                                               : Status::kLocalFailure);
                Outcome(message_sequence, size, status, Duration{0}, now);
                if (epoch != link_epoch) {
                  return;
                }
              }
            }
            if (observe) {
              config.on_local_write_complete(
                  observed_message_sequence,
                  adaptive_datagram_stream_internal::Elapsed(submitted_at,
                                                             queued_at),
                  adaptive_datagram_stream_internal::Elapsed(completed_at,
                                                             submitted_at),
                  s == WriteAction::Status::kSuccess);
              if (epoch != link_epoch) {
                return;
              }
            }
            pending_write->Complete(
                pending_write->cancelled ? WriteAction::Status::kStop : s);
            if (epoch != link_epoch) {
              return;
            }
          }
          Wake(clock());
        });
  }

  void Tick() {
    auto const epoch = link_epoch;
    auto const now = clock();
    // Reap actions only on a later scheduler turn, after their terminal event.
    pending.remove_if([](auto const& w) { return w->is_finished(); });
    for (auto& w : pending) {
      if (!w->submitted &&
          (w->cancelled ||
           now + std::min(latest_path_latency, config.latency_target / 2) >=
               w->deadline)) {
        auto const result =
            Result{w->packet.sequence,
                   w->cancelled ? Status::kCancelled : Status::kExpired,
                   w->packet.payload.size(), config.latency_target};
        w->submitted = true;
        Outcome(result.sequence, result.payload_bytes, result.status,
                result.latency, now);
        if (epoch != link_epoch) {
          return;
        }
        w->Complete(w->cancelled ? WriteAction::Status::kStop
                                 : WriteAction::Status::kFail);
        if (epoch != link_epoch) {
          return;
        }
      }
    }
    if (active_pending != nullptr &&
        (active_pending->cancelled || now >= active_pending->deadline)) {
      active->Stop();
      if (epoch != link_epoch) {
        return;
      }
    }
    while (!history.empty() && history.front().mature_at <= now) {
      auto const f = history.front();
      history.pop_front();
      flights.erase(f.sequence);
      Outcome(f.message_sequence, f.size,
              !f.received
                  ? (f.confirmed_missing ? Status::kLost : Status::kUnconfirmed)
                  : (f.on_time ? Status::kDelivered : Status::kLate),
              f.latency, now, f.path_latency, f.sent_at);
      if (epoch != link_epoch) {
        return;
      }
    }
    auto const it = std::find_if(
        pending.begin(), pending.end(),
        [](auto const& w) { return !w->submitted && !w->is_finished(); });
    auto const queue_age =
        it == pending.end() ? Duration{0}
                            : adaptive_datagram_stream_internal::Elapsed(
                                  now, (*it)->deadline - config.latency_target);
    controller.Update(now, queue_age);

    if (active == nullptr && lower->stream_info().is_writable &&
        now >= next_send) {
      if (peer_session != 0 && now >= next_feedback &&
          now <= last_receive + config.feedback_timeout) {
        Feedback feedback;
        feedback.session = peer_session;
        feedback.stream = config.stream_id;
        feedback.report = ++report_sequence;
        feedback.largest_sequence = largest_received;
        feedback.echo_sent_at_us = echo_sent;
        feedback.received_at_us = received_at;
        feedback.sampled_sequence = sampled_sequence;
        feedback.reported_at_us =
            adaptive_datagram_stream_internal::Micros(now);
        for (std::size_t i = 0; i < adaptive_datagram::kReceiptBits; ++i) {
          if (received[i]) {
            feedback.received[i / 64] |= std::uint64_t{1} << (i % 64);
          }
          if (on_time[i]) {
            feedback.on_time[i / 64] |= std::uint64_t{1} << (i % 64);
          }
        }
        auto const report_bytes = Pack(feedback).size();
        // Reserve at most one quarter of this direction's wire rate for
        // repeated feedback, so a low-rate bidirectional stream cannot starve.
        auto const report_gap_us = (report_bytes * std::uint64_t{4000000} +
                                    controller.rate_bytes_per_second() - 1) /
                                   controller.rate_bytes_per_second();
        next_feedback =
            now + std::max<std::chrono::microseconds>(
                      config.feedback_interval,
                      std::chrono::microseconds{
                          static_cast<std::int64_t>(report_gap_us)});
        Submit(Pack(feedback), nullptr, now);
      } else if (it != pending.end() &&
                 flights.size() < config.max_in_flight_packets &&
                 history.size() < config.max_history_packets &&
                 packet_sequence != std::numeric_limits<std::uint64_t>::max() &&
                 (!controller.metrics().feedback_stale || now >= next_probe)) {
        auto& w = **it;
        w.submitted = true;
        auto const message_sequence = w.packet.sequence;
        w.packet.sequence = ++packet_sequence;
        w.packet.sent_at_us = adaptive_datagram_stream_internal::Micros(now);
        history.push_back(Flight{w.packet.sequence, message_sequence,
                                 w.packet.payload.size(),
                                 w.deadline + config.feedback_timeout, now});
        flights.insert(w.packet.sequence);
        next_probe = now + config.feedback_timeout;
        Submit(Pack(w.packet), &w, now);
        if (epoch != link_epoch) {
          return;
        }
        w.packet.payload.clear();
      }
    }
    // Small bounded scheduling quantum also expires data during blocked writes.
    auto next = now + std::chrono::milliseconds{1};
    if (next_send > now) {
      next = std::min(next, next_send);
    }
    Wake(next);
  }

  void Receive(Datagram packet) {
    auto const now = clock();
    if (packet.session == 0 ||
        (config.peer_session_id != 0 &&
         packet.session != config.peer_session_id) ||
        packet.stream != config.stream_id || packet.sequence == 0 ||
        packet.lifetime_us == 0 || packet.payload.empty()) {
      ++stats.malformed;
      return;
    }
    if (peer_session != packet.session) {
      // An instance has one peer session. A new instance/session is negotiated
      // by its owner through Unlink/LinkOut, not by arbitrary delayed packets.
      if (peer_session != 0) {
        return;
      }
      peer_session = packet.session;
      largest_received = packet.sequence;
      received.reset();
      on_time.reset();
    }
    if (packet.sequence > largest_received) {
      auto const distance = packet.sequence - largest_received;
      auto const shift = static_cast<std::size_t>(
          std::min<std::uint64_t>(distance, adaptive_datagram::kReceiptBits));
      received <<= shift;
      on_time <<= shift;
      largest_received = packet.sequence;
    }
    auto const delta = largest_received - packet.sequence;
    if (delta >= adaptive_datagram::kReceiptBits ||
        received[static_cast<std::size_t>(delta)]) {
      ++stats.duplicates;
      return;
    }
    auto const local_sent = adaptive_datagram_stream_internal::LocalPeerTime(
        packet.queued_at_us, config.peer_clock_offset_us);
    auto const age = adaptive_datagram_stream_internal::Difference(
        static_cast<std::int64_t>(
            adaptive_datagram_stream_internal::Micros(now)),
        local_sent);
    auto const timely =
        age >= 0 && static_cast<std::uint64_t>(age) <= packet.lifetime_us;
    received[static_cast<std::size_t>(delta)] = true;
    on_time[static_cast<std::size_t>(delta)] = timely;
    echo_sent = packet.sent_at_us;
    received_at = adaptive_datagram_stream_internal::Micros(now);
    sampled_sequence = packet.sequence;
    last_receive = now;
    if (timely) {
      output.Emit(packet.payload);
    }
    Wake(now);
  }

  void Receive(Feedback const& feedback) {
    if (feedback.session != config.session_id ||
        feedback.stream != config.stream_id || feedback.report <= last_report ||
        feedback.largest_sequence > packet_sequence ||
        feedback.largest_sequence == 0) {
      return;
    }
    auto const now = clock();
    auto const now_us = adaptive_datagram_stream_internal::Micros(now);
    if (feedback.echo_sent_at_us > now_us) {
      return;
    }
    last_report = feedback.report;
    auto const sample = std::find_if(
        history.begin(), history.end(),
        [&](auto const& f) { return f.sequence == feedback.sampled_sequence; });
    auto const sample_delta =
        feedback.sampled_sequence <= feedback.largest_sequence
            ? feedback.largest_sequence - feedback.sampled_sequence
            : adaptive_datagram::kReceiptBits;
    auto const valid_sample =
        sample != history.end() &&
        sample_delta < adaptive_datagram::kReceiptBits &&
        sample->sent_at >= path_started_at_ &&
        (feedback.received[static_cast<std::size_t>(sample_delta / 64)] &
         (std::uint64_t{1} << (sample_delta % 64))) != 0 &&
        feedback.echo_sent_at_us ==
            adaptive_datagram_stream_internal::Micros(sample->sent_at) &&
        feedback.reported_at_us >= feedback.received_at_us;
    auto const fresh_sample =
        valid_sample && feedback.sampled_sequence > last_live_sample;
    auto rtt = controller.metrics().rtt;
    if (fresh_sample) {
      auto const hold = feedback.reported_at_us >= feedback.received_at_us
                            ? feedback.reported_at_us - feedback.received_at_us
                            : 0;
      auto const elapsed = now_us - feedback.echo_sent_at_us;
      rtt = Duration{static_cast<Duration::rep>(
          std::min<std::uint64_t>(elapsed - std::min(elapsed, hold),
                                  std::numeric_limits<Duration::rep>::max()))};
      last_live_sample = feedback.sampled_sequence;
    }
    controller.FeedbackReceived(now, rtt);
    std::uint64_t new_wire_bytes = 0;
    auto const local_report = adaptive_datagram_stream_internal::LocalPeerTime(
        feedback.reported_at_us, config.peer_clock_offset_us);
    for (auto& f : history) {
      auto const delta = f.sequence <= feedback.largest_sequence
                             ? feedback.largest_sequence - f.sequence
                             : adaptive_datagram::kReceiptBits;
      auto const missing_reported = f.sequence > feedback.largest_sequence ||
                                    delta < adaptive_datagram::kReceiptBits;
      auto const deadline = f.mature_at - config.feedback_timeout;
      if (!f.received && missing_reported &&
          local_report >=
              static_cast<std::int64_t>(
                  adaptive_datagram_stream_internal::Micros(deadline))) {
        f.confirmed_missing = true;
      }
      if (delta >= adaptive_datagram::kReceiptBits) {
        continue;
      }
      auto const mask = std::uint64_t{1} << (delta % 64);
      auto const receipt_index = static_cast<std::size_t>(delta / 64);
      if ((feedback.received[receipt_index] & mask) != 0) {
        if (!f.received && f.sent_at >= path_started_at_) {
          new_wire_bytes += f.wire_bytes;
        }
        flights.erase(f.sequence);
        f.received = true;
        f.on_time = (feedback.on_time[receipt_index] & mask) != 0;
        // Receipt masks carry the verdict for every packet. The echoed sample
        // additionally measures path delay without ACK scheduling/reverse
        // delay.
        if (fresh_sample && f.sequence == feedback.sampled_sequence) {
          auto const local_receive =
              adaptive_datagram_stream_internal::LocalPeerTime(
                  feedback.received_at_us, config.peer_clock_offset_us);
          auto const queued = deadline - config.latency_target;
          auto const age = adaptive_datagram_stream_internal::Difference(
              local_receive,
              static_cast<std::int64_t>(
                  adaptive_datagram_stream_internal::Micros(queued)));
          f.latency =
              Duration{static_cast<Duration::rep>(std::clamp<std::int64_t>(
                  age, 0, std::numeric_limits<Duration::rep>::max()))};
          auto const path_age = adaptive_datagram_stream_internal::Difference(
              local_receive,
              static_cast<std::int64_t>(
                  adaptive_datagram_stream_internal::Micros(f.sent_at)));
          f.path_latency =
              Duration{static_cast<Duration::rep>(std::clamp<std::int64_t>(
                  path_age, 0, std::numeric_limits<Duration::rep>::max()))};
          latest_path_latency = f.path_latency;
          controller.ObserveLatency(f.path_latency, f.sent_at);
        }
      }
    }
    if (!delivery_measurement_at_) {
      delivery_measurement_at_ = now;
      peer_measurement_at_ = feedback.reported_at_us;
    } else {
      acknowledged_wire_bytes_ += new_wire_bytes;
      auto const elapsed = adaptive_datagram_stream_internal::Elapsed(
                               now, *delivery_measurement_at_)
                               .count();
      auto const peer_elapsed =
          feedback.reported_at_us >= peer_measurement_at_
              ? feedback.reported_at_us - peer_measurement_at_
              : 0;
      auto const interval = std::max<std::uint64_t>(elapsed, peer_elapsed);
      if (fresh_sample && interval >= config.control_interval.count() &&
          acknowledged_wire_bytes_ != 0) {
        controller.ObserveDeliveryRate(
            now, acknowledged_wire_bytes_ * 1000000 / interval);
        delivery_measurement_at_ = now;
        peer_measurement_at_ = feedback.reported_at_us;
        acknowledged_wire_bytes_ = 0;
      }
    }
    Wake(now);
  }

  AeContext context;
  adaptive_datagram::Config config;
  Clock clock;
  adaptive_datagram::RateController controller;
  ProtocolContext protocol;
  adaptive_datagram_stream_internal::ApiSender sender;
  ApiReceiver receiver;
  StreamUpdateEvent updated;
  OutDataEvent output;
  DeliveryEvent delivered;
  ByteIStream* lower{};
  bool valid{};
  std::uint64_t link_epoch{};
  TaskSubscription task;
  Subscription data_sub;
  Subscription update_sub;
  Subscription write_sub;
  std::optional<adaptive_datagram_stream_internal::RejectedWrite> failed;
  std::list<std::unique_ptr<PendingWrite>> pending;
  // Outcome metadata survives ACK until maturation; only unacknowledged
  // sequence numbers consume send credit. Receipt-span eviction does not
  // imply delivery or loss: absent ACKs mature as unconfirmed.
  std::deque<Flight> history;
  std::set<std::uint64_t> flights;
  WriteAction* active{};
  PendingWrite* active_pending{};
  std::uint64_t sequence{};
  std::uint64_t packet_sequence{};
  std::uint64_t peer_session{};
  std::uint64_t largest_received{};
  std::uint64_t report_sequence{};
  std::uint64_t last_report{};
  std::uint64_t last_live_sample{};
  std::optional<TimePoint> delivery_measurement_at_;
  TimePoint path_started_at_{};
  std::uint64_t peer_measurement_at_{};
  std::uint64_t acknowledged_wire_bytes_{};
  std::uint64_t echo_sent{};
  std::uint64_t received_at{};
  std::uint64_t sampled_sequence{};
  std::bitset<adaptive_datagram::kReceiptBits> received;
  std::bitset<adaptive_datagram::kReceiptBits> on_time;
  TimePoint scheduled_at{};
  TimePoint next_send{};
  TimePoint next_feedback{};
  TimePoint next_probe{};
  TimePoint last_receive{};
  Duration latest_path_latency{0};
  adaptive_datagram::StreamStatistics stats;
};

AdaptiveDatagramStream::AdaptiveDatagramStream(AeContext const& context,
                                               adaptive_datagram::Config config,
                                               Clock clock)
    : impl_{std::make_shared<Impl>(context, config, std::move(clock))} {}
AdaptiveDatagramStream::~AdaptiveDatagramStream() { impl_->Unlink(false); }
void AdaptiveDatagramStream::LinkOut(ByteIStream& stream) {
  auto impl = impl_;
  impl->Link(stream);
}
void AdaptiveDatagramStream::Unlink() {
  auto impl = impl_;
  impl->Unlink();
}
WriteAction& AdaptiveDatagramStream::Write(DataBuffer&& data) {
  return impl_->Write(std::move(data));
}
StreamInfo AdaptiveDatagramStream::stream_info() const { return impl_->Info(); }
AdaptiveDatagramStream::StreamUpdateEvent const&
AdaptiveDatagramStream::stream_update_event() {
  return impl_->updated;
}
AdaptiveDatagramStream::OutDataEvent const&
AdaptiveDatagramStream::out_data_event() {
  return impl_->output;
}
AdaptiveDatagramStream::DeliveryEvent const&
AdaptiveDatagramStream::delivery_event() {
  return impl_->delivered;
}
void AdaptiveDatagramStream::Restream() {
  auto impl = impl_;
  impl->ResetPath();
  if (impl->lower != nullptr) {
    impl->lower->Restream();
  }
}
adaptive_datagram::StreamStatistics AdaptiveDatagramStream::statistics() const {
  auto stats = impl_->stats;
  stats.queued = impl_->Queued();
  stats.in_flight = impl_->flights.size();
  return stats;
}
adaptive_datagram::RateControllerMetrics const&
AdaptiveDatagramStream::metrics() const {
  return impl_->controller.metrics();
}
std::uint64_t AdaptiveDatagramStream::rate_bytes_per_second() const {
  return impl_->controller.rate_bytes_per_second();
}
bool AdaptiveDatagramStream::valid() const { return impl_->valid; }
}  // namespace ae
