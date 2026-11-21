// Copyright 2026 Aethernet Inc. Licensed under the Apache License, Version 2.0.
// Windows deterministic network lab. The network and observers live exclusively
// here. The sender sees only admission pressure and receipts returned on wire.
#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <queue>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include "aether/client_messages/raw_resource.h"
namespace {
using Tick = ae::RawResource::Tick;
constexpr Tick kSecond = 1000000, kStep = 200, kDuration = 40 * kSecond;
unsigned seed = 20261121;
constexpr unsigned kLinkBytes = 256 * 1024, kOuter = 128;
struct Scenario {
  std::string name;
  unsigned flows, rtt_ms, loss_percent;
  bool weighted{}, classes{}, churn{}, burst{}, ramp{};
};
unsigned Noise(unsigned key, unsigned lane) {
  unsigned x = key ^ (seed + lane * 0x9e3779b9u);
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  return x ^ (x >> 16);
}
struct Message {
  Tick offered{};
  unsigned flow{}, number{}, useful{}, charged{};
  ae::RawReceipt tag{};
};
struct Transit {
  Tick due;
  Message packet;
  bool operator<(Transit const& rhs) const {
    return std::tie(due, packet.flow, packet.number) >
           std::tie(rhs.due, rhs.packet.flow, rhs.packet.number);
  }
};
using TransitQueue = std::priority_queue<Transit>;
struct Link {
  struct Item {
    Message packet;
    unsigned remaining;
    Tick queued;
  };
  std::deque<Item> queue;
  unsigned bytes{}, remainder{}, limit;
  explicit Link(unsigned capacity) : limit{capacity} {}
  bool Push(Message m, Tick now) {
    if (m.charged > limit - bytes) {
      return false;
    }
    queue.push_back({m, m.charged, now});
    bytes += m.charged;
    return true;
  }
  template <class F>
  void Serve(unsigned bits_per_sec, Tick now, F deliver) {
    auto credit =
        static_cast<unsigned long long>(bits_per_sec) * kStep + remainder;
    auto budget = static_cast<unsigned>(credit / 8000000);
    remainder = static_cast<unsigned>(credit % 8000000);
    while (!queue.empty() && budget) {
      auto& head = queue.front();
      auto taken = std::min(head.remaining, budget);
      bytes -= taken;
      head.remaining -= taken;
      budget -= taken;
      if (!head.remaining) {
        deliver(head.packet, now);
        queue.pop_front();
      }
    }
    if (queue.empty()) {
      remainder = 0;
    }
  }
  Tick Age(Tick now) const {
    return queue.empty() ? 0 : now - queue.front().queued;
  }
};
struct Counts {
  unsigned offered{}, accepted{}, sent{}, delivered{}, expired{}, lost{},
      useful{};
  unsigned long long offered_bytes{}, accepted_bytes{};
  std::vector<unsigned> delay;
};
struct Flow {
  ae::StreamSendPolicy policy{};
  unsigned slot{}, weight{1}, sequence{};
  Tick next{}, next_receipt{};
  unsigned window_bytes{};
  std::vector<unsigned> window_delays;
  std::deque<Message> waiting;
  Counts count;
};
struct Writers {
  std::ofstream bins, totals, allocation, latency;
  explicit Writers(std::filesystem::path const& dir)
      : bins(dir / "windows.tsv"),
        totals(dir / "flows.tsv"),
        allocation(dir / "allocation.tsv"),
        latency(dir / "latency.tsv") {
    if (!bins || !totals || !allocation || !latency) {
      throw std::runtime_error("Output directory unavailable");
    }
    allocation << "scenario\tmode\tsecond\tflow\tuseful_B\tp95_us\tp99_us\n";
    latency << "scenario\tmode\tp50_us\tp95_us\tp99_us\n";
    bins << "scenario\tmode\tsecond\tcapacity_bps\tpace_Bps\tuseful_"
            "bytes\tsent_charge\tlink_peak_B\tlink_age_us\tlocal_peak_B\tlocal_"
            "age_us\tflight_peak_B\tack_sent\tack_seen\tack_charge\n";
    totals << "scenario\tmode\tflow\tpriority\tweight\toffered\taccepted\tsent"
              "\tdelivered\tlost\texpired\toffered_B\taccepted_B\tuseful_"
              "B\tp50_us\tp95_us\tp99_us\n";
  }
};
unsigned Capacity(Scenario const& s, Tick t) {
  if (s.burst && t >= 18 * kSecond && t < 18 * kSecond + 450000) {
    return 0;
  }
  if (s.ramp) {
    return static_cast<unsigned>(
        (8.0 + 6.0 * std::cos(6.283185307179586 * static_cast<double>(t) /
                              (22 * kSecond))) *
        1000000);
  }
  return t < 12 * kSecond ? 20000000u : t < 24 * kSecond ? 2000000u : 10000000u;
}
unsigned Quantile(std::vector<unsigned> const& values, double p) {
  return values.empty()
             ? 0u
             : values[static_cast<std::size_t>(std::ceil(values.size() * p)) -
                      1];
}
struct Window {
  unsigned delivered{}, sent{}, link{}, local{}, flight{}, replies{},
      receipts{}, feedback_bytes{};
  Tick link_age{}, local_age{};
};
void Run(Scenario const& s, bool balanced, Writers& out) {
  ae::RawResourceConfig cfg;
  cfg.max_flows = s.flows;
  cfg.slots_per_flow = 2;
  cfg.target_extra_rtt_us = s.rtt_ms == 300 ? 50000 : 30000;
  ae::RawResource sender{cfg};
  std::vector<Flow> flows(s.flows);
  constexpr std::array classes{
      ae::SendPriority::kInteractive, ae::SendPriority::kRealtime,
      ae::SendPriority::kNormal, ae::SendPriority::kBackground};
  for (unsigned i = 0; i < s.flows; ++i) {
    auto& f = flows[i];
    f.policy.priority = s.classes ? classes[i % 4] : ae::SendPriority::kNormal;
    f.weight = s.weighted && i % 2 ? 3u : 1u;
    f.slot = sender.Open(f.policy.priority, f.weight);
    f.next = Noise(i, 0) % 370;
    if (s.churn && i >= s.flows / 2) {
      f.next += 8 * kSecond;
    }
  }
  Link forward{kLinkBytes}, reverse{8192};
  TransitQueue arriving, returning;
  Window bin;
  unsigned queued{};
  auto mode = balanced ? "balanced" : "raw";
  auto submit = [&](Message m, Tick now) {
    auto& c = flows[m.flow].count;
    ++c.sent;
    bin.sent += m.charged;
    if (!forward.Push(m, now)) {
      ++c.lost;
    }
  };
  for (Tick t = 0; t < kDuration + 3 * kSecond; t += kStep) {
    auto capacity = Capacity(s, t);
    auto jitter = [&](Message const& m, unsigned lane) -> Tick {
      return static_cast<Tick>(Noise(m.number * 37u + m.flow, lane) % 4001);
    };
    forward.Serve(capacity, t, [&](Message m, Tick when) {
      if (Noise(m.number * 37u + m.flow, 1) % 100 < s.loss_percent ||
          (s.burst && when >= 17 * kSecond && when < 17 * kSecond + 130000)) {
        ++flows[m.flow].count.lost;
        return;
      }
      Tick base = s.rtt_ms * 350 + (m.flow % 4) * 1500;
      arriving.push({when + base + jitter(m, 2), m});
    });
    reverse.Serve(6000000, t, [&](Message m, Tick when) {
      if (Noise(m.number * 37u + m.flow, 3) % 100 < s.loss_percent) {
        return;
      }
      Tick base = s.rtt_ms * 650 + (m.flow % 4) * 800;
      returning.push({when + base + jitter(m, 4), m});
    });
    while (!arriving.empty() && arriving.top().due <= t) {
      auto m = arriving.top().packet;
      arriving.pop();
      auto& c = flows[m.flow].count;
      ++c.delivered;
      c.useful += m.useful;
      bin.delivered += m.useful;
      c.delay.push_back(static_cast<unsigned>(t - m.offered));
      auto& dest = flows[m.flow];
      dest.window_bytes += m.useful;
      dest.window_delays.push_back(c.delay.back());
      if (balanced && t >= dest.next_receipt) {
        // One outstanding local feedback write per sink, completed next tick.
        dest.next_receipt = t + kStep;
        // The receiver has only this frame. Encode/decode the actual wire
        // fields.
        auto header = ae::ReadRawHeader(ae::RawFrame(m.tag, {}), false).value();
        m.tag = ae::ReadRawHeader(ae::RawAck(header), true).value();
        m.charged = kOuter + ae::kRawHeaderBytes;
        ++bin.replies;
        bin.feedback_bytes += m.charged;
        reverse.Push(m, t);
      }
    }
    while (!returning.empty() && returning.top().due <= t) {
      auto m = returning.top().packet;
      returning.pop();
      if (sender.Receipt(flows[m.flow].slot, m.tag.sequence, t)) {
        ++bin.receipts;
      }
    }
    if (balanced) {
      sender.Update(t);
    }
    std::array<Message, 32> batch{};
    unsigned batch_size{};
    for (unsigned j = 0; j < s.flows; ++j) {
      unsigned id = (j + static_cast<unsigned>(t / kStep)) % s.flows;
      auto& f = flows[id];
      auto cutoff = s.churn && id >= s.flows / 2 ? 28 * kSecond : kDuration;
      while (f.next <= t && f.next < cutoff) {
        bool interactive = s.classes && id == 0;
        unsigned size = interactive ? 72u : 1000u;
        Message m{f.next, id, ++f.sequence, size,
                  size + kOuter + (balanced ? ae::kRawHeaderBytes : 0)};
        f.next += interactive ? 20000 : 370;
        auto& c = f.count;
        ++c.offered;
        c.offered_bytes += size;
        if (balanced && !sender.CanQueue(f.slot, m.charged)) {
          continue;
        }
        ++c.accepted;
        c.accepted_bytes += size;
        if (balanced) {
          sender.Queued(f.slot, m.charged);
          f.waiting.push_back(m);
          queued += m.charged;
        } else {
          batch.at(batch_size++) = m;
        }
      }
    }
    if (!balanced) {
      // Plain Raw has the client's strict priority FIFO. The local UDP writer
      // accepts immediately; simultaneous offers are its only local backlog.
      std::stable_sort(batch.begin(), batch.begin() + batch_size,
                       [&](auto const& a, auto const& b) {
                         return flows[a.flow].policy.priority <
                                flows[b.flow].policy.priority;
                       });
      unsigned local{};
      for (unsigned k = 0; k < batch_size; ++k) {
        local += batch[k].charged;
        submit(batch[k], t);
      }
      bin.local = std::max(bin.local, local);
    }
    if (balanced) {
      std::array<ae::RawHead, 32> heads{};
      unsigned count{};
      for (auto& f : flows) {
        while (!f.waiting.empty() &&
               t - f.waiting.front().offered >= cfg.queue_lifetime_us) {
          auto bytes = f.waiting.front().charged;
          f.waiting.pop_front();
          queued -= bytes;
          sender.Removed(f.slot, bytes);
          ++f.count.expired;
        }
        if (!f.waiting.empty()) {
          heads[count++] = {f.slot, f.waiting.front().charged};
          bin.local_age =
              std::max(bin.local_age, t - f.waiting.front().offered);
        }
      }
      bin.local = std::max(bin.local, queued);
      if (auto pick = sender.Choose(std::span{heads}.first(count), t)) {
        auto& f = flows[*pick];
        auto m = f.waiting.front();
        f.waiting.pop_front();
        m.tag = {1, sender.next_sequence()};
        sender.Sent(f.slot, m.charged, t);
        sender.Removed(f.slot, m.charged);
        queued -= m.charged;
        submit(m, t);
      }
    }
    bin.link = std::max(bin.link, forward.bytes);
    bin.link_age = std::max(bin.link_age, forward.Age(t));
    bin.flight = std::max(bin.flight, sender.in_flight());
    if ((t + kStep) % kSecond == 0) {
      out.bins << s.name << '\t' << mode << '\t' << t / kSecond << '\t'
               << capacity << '\t' << (balanced ? sender.rate() : 0) << '\t'
               << bin.delivered << '\t' << bin.sent << '\t' << bin.link << '\t'
               << bin.link_age << '\t' << bin.local << '\t' << bin.local_age
               << '\t' << bin.flight << '\t' << bin.replies << '\t'
               << bin.receipts << '\t' << bin.feedback_bytes << '\n';
      for (unsigned id = 0; id < s.flows; ++id) {
        auto& f = flows[id];
        auto& d = f.window_delays;
        std::sort(d.begin(), d.end());
        out.allocation << s.name << '\t' << mode << '\t' << t / kSecond << '\t'
                       << id << '\t' << f.window_bytes << '\t'
                       << Quantile(d, .95) << '\t' << Quantile(d, .99) << '\n';
        d.clear();
        f.window_bytes = 0;
      }
      bin = {};
    }
  }
  std::vector<unsigned> all_delays;
  for (unsigned i = 0; i < s.flows; ++i) {
    auto& f = flows[i];
    auto& c = f.count;
    all_delays.insert(all_delays.end(), c.delay.begin(), c.delay.end());
    if (c.accepted != c.sent + c.expired || c.sent != c.delivered + c.lost ||
        !c.delivered) {
      throw std::runtime_error(s.name + ": packet conservation or starvation");
    }
    std::sort(c.delay.begin(), c.delay.end());
    out.totals << s.name << '\t' << mode << '\t' << i << '\t'
               << static_cast<unsigned>(f.policy.priority) << '\t' << f.weight
               << '\t' << c.offered << '\t' << c.accepted << '\t' << c.sent
               << '\t' << c.delivered << '\t' << c.lost << '\t' << c.expired
               << '\t' << c.offered_bytes << '\t' << c.accepted_bytes << '\t'
               << c.useful << '\t' << Quantile(c.delay, .50) << '\t'
               << Quantile(c.delay, .95) << '\t' << Quantile(c.delay, .99)
               << '\n';
  }
  std::sort(all_delays.begin(), all_delays.end());
  out.latency << s.name << '\t' << mode << '\t' << Quantile(all_delays, .5)
              << '\t' << Quantile(all_delays, .95) << '\t'
              << Quantile(all_delays, .99) << '\n';
}
}  // namespace
int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    return 2;
  }
  if (argc == 3) {
    seed = static_cast<unsigned>(std::stoul(argv[2]));
  }
  Writers out{argv[1]};
  std::vector<Scenario> cases{
      {"one20", 1, 20, 0},
      {"pair100", 2, 100, 0},
      {"weighted", 2, 100, 0, true},
      {"eight1pct", 8, 100, 1},
      {"many300", 32, 300, 5},
      {"classes", 8, 100, 1, false, true},
      {"churn", 8, 20, 0, false, false, true},
      {"outage", 8, 100, 1, false, false, false, true},
      {"ramp", 8, 20, 0, false, false, false, false, true}};
  for (auto const& s : cases) {
    Run(s, false, out);
    Run(s, true, out);
  }
}
