#define AE_EXAMPLE_ETHERNET 1
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <list>
#include <string_view>
#include <vector>
#include "aether/ae_actions/select_client.h"
#include "aether/client_messages/p2p_message_stream.h"
#include "aether_construct_ethernet.h"
#include "stream_matrix/bench_persistence.h"

namespace ae::p2p_throughput_internal {
using Clock = std::chrono::steady_clock;
std::uint64_t Micros() {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          Clock::now().time_since_epoch())
          .count());
}
void Put(DataBuffer& d, std::size_t at, std::uint64_t value) {
  for (unsigned i = 0; i < 8; ++i) {
    d[at + i] = static_cast<std::uint8_t>(value >> (8 * i));
  }
}
std::uint64_t Get(DataBuffer const& d, std::size_t at) {
  std::uint64_t value{};
  for (unsigned i = 0; i < 8; ++i) {
    value |= std::uint64_t{d[at + i]} << (8 * i);
  }
  return value;
}
std::uint64_t Percentile(std::vector<std::uint64_t> d, unsigned p) {
  if (d.empty()) {
    return 0;
  }
  auto at = (d.size() * p + 99) / 100 - 1;
  std::nth_element(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(at),
                   d.end());
  return d[at];
}
bool Until(AetherApp& app, auto predicate, unsigned seconds) {
  auto deadline = Clock::now() + std::chrono::seconds{seconds};
  while (!app.IsExited() && Clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    auto wake = app.Update(Now());
    app.WaitUntil(std::min(wake, Now() + std::chrono::milliseconds{1}));
  }
  return predicate();
}
int Run(char const* filename, unsigned seconds, unsigned payload, bool udp) {
  if (!AE_FILTRATION || !stream_matrix_bench::RegistrationSeedReadyForUdpBench(
                            std::cerr, std::filesystem::current_path())) {
    return 3;
  }
  auto app = examples::construct_aether_app();
  Client::ptr a, b;
  bool failed = false;
  MultiSubscription selections;
  auto parent = Uid::FromString("3ac93165-3d37-4970-87a6-fa4ee27744e4");
  selections += app->aether()
                    ->SelectClient(parent, "sender")
                    .result_event()
                    .Subscribe([&](auto const& r) {
                      if (r) {
                        a = r.value();
                      } else {
                        failed = true;
                      }
                    });
  selections += app->aether()
                    ->SelectClient(parent, "receiver")
                    .result_event()
                    .Subscribe([&](auto const& r) {
                      if (r) {
                        b = r.value();
                      } else {
                        failed = true;
                      }
                    });
  if (!Until(*app, [&] { return (a && b) || failed; }, 60) || !a || !b) {
    return 3;
  }
  auto ca = a.Load(), cb = b.Load();
  ca->connectivity_policy()
      ->ConfigureRxTimings(RequestPolicy::All{})
      .ForPriority<0>(RxTimingConf::Every(std::chrono::seconds{1}));
  cb->connectivity_policy()
      ->ConfigureRxTimings(RequestPolicy::All{})
      .ForPriority<0>(RxTimingConf::Every(std::chrono::seconds{1}));
  auto context = AeContext{*app};
  P2pStream sender{context, ca, cb->uid(),
                   ca->message_stream_manager().CreatePort(cb->uid())};
  P2pStream receiver{context, cb, ca->uid(),
                     cb->message_stream_manager().CreatePort(ca->uid())};
  if (!Until(
          *app,
          [&] {
            return sender.stream_info().link_state == LinkState::kLinked &&
                   receiver.stream_info().link_state == LinkState::kLinked;
          },
          60)) {
    return 5;
  }
  if (sender.stream_info().max_element_size < payload) {
    std::cerr << "Payload exceeds actual P2pStream budget\n";
    return 2;
  }
  std::ofstream csv{filename};
  if (!csv) {
    return 2;
  }
  std::uint64_t offered{}, received{}, measured_offered{}, measured_received{},
      duplicates{}, corrupt{}, out_of_order{}, largest{}, local_failures{},
      sync_unobserved{};
  auto run = Micros();
  std::vector<bool> seen;
  std::vector<std::uint64_t> latencies;
  auto on_data = [&](DataBuffer const& d) {
    if (d.size() != payload || Get(d, 0) != run || Get(d, 8) == 0 ||
        Get(d, 8) > offered || Get(d, 16) > Micros()) {
      ++corrupt;
      return;
    }
    auto seq = Get(d, 8);
    for (std::size_t i = 32; i < d.size(); ++i) {
      if (d[i] != static_cast<std::uint8_t>(seq + i)) {
        ++corrupt;
        return;
      }
    }
    if (seen.size() <= seq) {
      seen.resize(static_cast<std::size_t>(seq + 1));
    }
    if (seen[static_cast<std::size_t>(seq)]) {
      ++duplicates;
      return;
    }
    seen[static_cast<std::size_t>(seq)] = true;
    if (seq < largest) {
      ++out_of_order;
    }
    largest = std::max(largest, seq);
    received += d.size();
    if (Get(d, 24) == 1) {
      measured_received += d.size();
    }
    latencies.push_back(Micros() - Get(d, 16));
  };
  // The event system has bounded handler storage. Keep the measurement state
  // on this stack frame and register only a reference to its callback.
  Subscription receive = receiver.out_data_event().Subscribe(
      [&on_data](DataBuffer const& data) { on_data(data); });
  struct Write {
    bool done = false;
    Subscription subscription;
  };
  std::list<Write> writes;
  auto start = Clock::now(), measure_start = start + std::chrono::seconds{3};
  auto measure_end = measure_start + std::chrono::seconds{seconds},
       end = measure_end + std::chrono::seconds{5};
  unsigned row{};
  std::uint64_t previous_offered{}, previous_received{};
  csv << "second,phase,offered_Bps,unique_goodput_Bps,duplicates,corrupt,local_"
         "failures,sync_status_unobserved,open_writes,tcp,udp,latency_p50_us,"
         "latency_p95_us,latency_p99_us,out_of_order\n";
  while (!app->IsExited() && Clock::now() < end) {
    auto now = Clock::now();
    bool measure = now >= measure_start && now < measure_end;
    auto path = sender.InspectSendPathTransport();
    if ((udp && (!path.linked_udp_endpoint || path.linked_tcp_endpoint)) ||
        (!udp && (!path.linked_tcp_endpoint || path.linked_udp_endpoint))) {
      std::cerr << "Send path does not match requested transport\n";
      return 5;
    }
    writes.remove_if([](Write const& w) { return w.done; });
    if (now < measure_end && writes.size() < 128 &&
        sender.stream_info().is_writable) {
      DataBuffer d(payload, 0);
      Put(d, 0, run);
      Put(d, 8, ++offered);
      Put(d, 16, Micros());
      Put(d, 24, measure ? 1 : 0);
      for (std::size_t i = 32; i < d.size(); ++i) {
        d[i] = static_cast<std::uint8_t>(offered + i);
      }
      if (measure) {
        measured_offered += payload;
      }
      writes.emplace_back();
      auto& w = writes.back();
      auto& action = sender.Write(std::move(d));
      if (action.is_finished()) {
        w.done = true;
        ++sync_unobserved;
      } else {
        w.subscription =
            action.status_event().Subscribe([&w, &local_failures](auto status) {
              w.done = true;
              if (status != WriteAction::Status::kSuccess) {
                ++local_failures;
              }
            });
      }
    }
    app->Update(Now());
    auto elapsed =
        std::chrono::duration_cast<std::chrono::seconds>(now - start).count();
    if (elapsed > row) {
      ++row;
      csv << row << ','
          << (now < measure_start ? "warmup"
              : now < measure_end ? "measure"
                                  : "drain")
          << ',' << (offered - previous_offered) * payload << ','
          << received - previous_received << ',' << duplicates << ',' << corrupt
          << ',' << local_failures << ',' << sync_unobserved << ','
          << writes.size() << ',' << path.linked_tcp_endpoint << ','
          << path.linked_udp_endpoint << ',' << Percentile(latencies, 50) << ','
          << Percentile(latencies, 95) << ',' << Percentile(latencies, 99)
          << ',' << out_of_order << '\n';
      csv.flush();
      previous_offered = offered;
      previous_received = received;
      latencies.clear();
    }
    if (now >= measure_end) {
      app->WaitUntil(Now() + std::chrono::milliseconds{1});
    }
  }
  bool okay = corrupt == 0 && measured_received > 0 && csv.good();
  std::cout << "p2p_throughput status=" << (okay ? "PASS" : "FAIL")
            << " transport=" << (udp ? "UDP" : "TCP")
            << " measured_goodput_Bps=" << measured_received / seconds
            << " offered_bytes=" << measured_offered
            << " received_bytes=" << measured_received << " payload=" << payload
            << " local_failures=" << local_failures
            << " duplicates=" << duplicates << " corrupt=" << corrupt
            << " out_of_order=" << out_of_order << '\n';
  return okay ? 0 : 1;
}
}  // namespace ae::p2p_throughput_internal
int main(int argc, char** argv) {
  if (argc != 5) {
    std::cerr << "Usage: p2p-throughput CSV seconds payload tcp|udp\n";
    return 2;
  }
  unsigned seconds{}, payload{};
  auto parse = [](char const* s, unsigned& v) {
    std::string_view t{s};
    auto r = std::from_chars(t.data(), t.data() + t.size(), v);
    return r.ec == std::errc{} && r.ptr == t.data() + t.size();
  };
  if (!parse(argv[2], seconds) || !parse(argv[3], payload) || !seconds ||
      seconds > 600 || payload < 32 || payload > 65535) {
    return 2;
  }
  auto mode = std::string_view{argv[4]};
  if (mode != "tcp" && mode != "udp") {
    return 2;
  }
  return ae::p2p_throughput_internal::Run(argv[1], seconds, payload,
                                          mode == "udp");
}
