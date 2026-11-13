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

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>

#include "saturation_scenario.h"

int main(int argc, char* argv[]) {
  if (argc > 3) {
    std::cerr
        << "Usage: adaptive-datagram-saturation [runs.csv] [summary.csv]\n";
    return 2;
  }
  std::ofstream runs{argc >= 2 ? argv[1] : "saturation-runs.csv"};
  std::ofstream summary{argc >= 3 ? argv[2] : "saturation-summary.csv"};
  if (!runs || !summary) {
    std::cerr << "Cannot open CSV output.\n";
    return 1;
  }
  runs << "pattern,target_load_pct,seed,capacity_Bps,packet_bytes,queue_"
          "packets,offered_packets,received_packets,queue_drops,random_drops,"
          "other_drops,offered_Bps,goodput_Bps,loss_pct,delay_p50_us,delay_p95_"
          "us,delay_max_us,peak_queue_bytes,accounting_ok\n";
  summary << "pattern,target_load_pct,seeds,offered_Bps_mean,goodput_Bps_mean,"
             "loss_pct_weighted,delay_p50_us_mean,delay_p95_us_mean,delay_max_"
             "us,accounting_ok\n";
  constexpr std::array<std::uint64_t, 5> seeds{12345, 24680, 7, 42, 999};
  constexpr std::array<unsigned, 11> loads{500,  700,  850,  900,  950, 980,
                                           1000, 1050, 1100, 1250, 1500};
  runs << std::fixed << std::setprecision(6);
  summary << std::fixed << std::setprecision(6);
  bool valid = true;
  for (auto pattern : {ae::saturation::ArrivalPattern::kPoisson,
                       ae::saturation::ArrivalPattern::kPeriodic}) {
    auto const* name = pattern == ae::saturation::ArrivalPattern::kPoisson
                           ? "poisson"
                           : "periodic";
    for (auto load : loads) {
      double offered = 0, goodput = 0, p50 = 0, p95 = 0;
      std::uint64_t sent = 0, dropped = 0;
      std::uint32_t maximum = 0;
      bool accounted = true;
      for (auto seed : seeds) {
        ae::saturation::Config config;
        config.pattern = pattern;
        config.load_per_mille = load;
        config.seed = seed;
        auto const r = ae::saturation::Run(config);
        accounted = accounted && r.accounting_ok && r.local_writes_ok &&
                    r.random_drops == 0 && r.other_drops == 0;
        sent += r.offered_packets;
        dropped += r.queue_drops;
        offered += r.offered_Bps;
        goodput += r.goodput_Bps;
        p50 += r.delay_p50_us;
        p95 += r.delay_p95_us;
        maximum = std::max(maximum, r.delay_max_us);
        runs << name << ',' << load / 10.0 << ',' << seed << ','
             << config.capacity_Bps << ',' << config.packet_bytes << ','
             << config.queue_packets << ',' << r.offered_packets << ','
             << r.received_packets << ',' << r.queue_drops << ','
             << r.random_drops << ',' << r.other_drops << ',' << r.offered_Bps
             << ',' << r.goodput_Bps << ',' << r.loss_pct << ','
             << r.delay_p50_us << ',' << r.delay_p95_us << ',' << r.delay_max_us
             << ',' << r.peak_queue_bytes << ',' << r.accounting_ok << '\n';
      }
      auto const loss =
          100.0 * static_cast<double>(dropped) / static_cast<double>(sent);
      summary << name << ',' << load / 10.0 << ',' << seeds.size() << ','
              << offered / seeds.size() << ',' << goodput / seeds.size() << ','
              << loss << ',' << p50 / seeds.size() << ',' << p95 / seeds.size()
              << ',' << maximum << ',' << accounted << '\n';
      std::cout << name << " load=" << load / 10.0 << "% loss=" << loss
                << "% p95=" << p95 / seeds.size() / 1000
                << "ms goodput=" << goodput / seeds.size() << "B/s\n";
      valid = valid && accounted;
    }
  }
  runs.flush();
  summary.flush();
  return valid && runs && summary ? 0 : 1;
}
