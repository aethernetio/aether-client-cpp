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

#ifndef EXAMPLES_PROBE_RECEIVER_PRODUCT_PROBE_SELECT_H_
#define EXAMPLES_PROBE_RECEIVER_PRODUCT_PROBE_SELECT_H_

#include <cstddef>
#include <cstdint>

// Parameter selection for the product adaptive Wi-Fi probe. Everything here is
// pure computation over plain data so the same code runs on the ESP32 firmware
// and in host unit tests: no Wi-Fi, no clock, no allocation. Time is always
// supplied by the caller in milliseconds.

namespace ae::probe {

// Number of reconnects in one ICMP profile trial.
#ifndef AE_PRODUCT_PROBE_ICMP_BATCH
#  define AE_PRODUCT_PROBE_ICMP_BATCH 5
#endif
// Additional reconnects appended when the first trial is borderline.
#ifndef AE_PRODUCT_PROBE_ICMP_EXTEND
#  define AE_PRODUCT_PROBE_ICMP_EXTEND 5
#endif
// ICMP echo requests sent per successful connect.
#ifndef AE_PRODUCT_PROBE_ICMP_PER_CONNECT
#  define AE_PRODUCT_PROBE_ICMP_PER_CONNECT 3
#endif
// Accepted ICMP reply ratio, as a fraction.
#ifndef AE_PRODUCT_PROBE_ICMP_ACCEPT_NUM
#  define AE_PRODUCT_PROBE_ICMP_ACCEPT_NUM 9
#endif
#ifndef AE_PRODUCT_PROBE_ICMP_ACCEPT_DEN
#  define AE_PRODUCT_PROBE_ICMP_ACCEPT_DEN 10
#endif
// Packets per parameter batch and the near-miss tolerance that buys one
// additional batch instead of rejecting the parameter outright.
#ifndef AE_PRODUCT_PROBE_BATCH_SIZE
#  define AE_PRODUCT_PROBE_BATCH_SIZE 20
#endif
#ifndef AE_PRODUCT_PROBE_BATCH_TOLERANCE
#  define AE_PRODUCT_PROBE_BATCH_TOLERANCE 1
#endif
// Late-arrival query: stop re-querying once the unique count has not moved for
// this long, and give up entirely after the timeout.
#ifndef AE_PRODUCT_PROBE_QUERY_STABLE_MS
#  define AE_PRODUCT_PROBE_QUERY_STABLE_MS 500
#endif
#ifndef AE_PRODUCT_PROBE_QUERY_TIMEOUT_MS
#  define AE_PRODUCT_PROBE_QUERY_TIMEOUT_MS 1500
#endif
// Production hot run length and the deep sleep between hot sends.
#ifndef AE_PRODUCT_PROBE_HOT_COUNT
#  define AE_PRODUCT_PROBE_HOT_COUNT 100
#endif
#ifndef AE_PRODUCT_PROBE_SLEEP_MS
#  define AE_PRODUCT_PROBE_SLEEP_MS 250
#endif

static constexpr std::uint16_t kProbeIcmpBatch = AE_PRODUCT_PROBE_ICMP_BATCH;
static constexpr std::uint16_t kProbeIcmpExtend = AE_PRODUCT_PROBE_ICMP_EXTEND;
static constexpr std::uint16_t kProbeIcmpPerConnect =
    AE_PRODUCT_PROBE_ICMP_PER_CONNECT;
static constexpr std::uint16_t kProbeBatchSize = AE_PRODUCT_PROBE_BATCH_SIZE;
static constexpr std::uint16_t kProbeBatchTolerance =
    AE_PRODUCT_PROBE_BATCH_TOLERANCE;
static constexpr std::uint32_t kProbeQueryStableMs =
    AE_PRODUCT_PROBE_QUERY_STABLE_MS;
static constexpr std::uint32_t kProbeQueryTimeoutMs =
    AE_PRODUCT_PROBE_QUERY_TIMEOUT_MS;
static constexpr std::uint16_t kProbeHotCount = AE_PRODUCT_PROBE_HOT_COUNT;
static constexpr std::uint16_t kProbeSleepMs = AE_PRODUCT_PROBE_SLEEP_MS;

// Stages of one full self-configuration run. A cold boot always restarts at
// kIcmpSelect; every other transition is a single step forward.
enum class ProbeStage : std::uint8_t {
  kIcmpSelect = 0,
  kFullPrepare = 1,
  kPostProbe = 2,
  kPostQuery = 3,
  kSleepConfirm = 4,
  kSleepConfirmQuery = 5,
  kProbeComplete = 6,
  kHotRun = 7,
  kHotSummary = 8,
  kDone = 9,
};

static constexpr std::uint8_t kProbeStageCount = 10;

inline char const* ProbeStageName(ProbeStage stage) {
  switch (stage) {
    case ProbeStage::kIcmpSelect:
      return "ICMP_SELECT";
    case ProbeStage::kFullPrepare:
      return "FULL_PREPARE";
    case ProbeStage::kPostProbe:
      return "POST_PROBE";
    case ProbeStage::kPostQuery:
      return "POST_QUERY";
    case ProbeStage::kSleepConfirm:
      return "SLEEP_CONFIRM";
    case ProbeStage::kSleepConfirmQuery:
      return "SLEEP_CONFIRM_QUERY";
    case ProbeStage::kProbeComplete:
      return "PROBE_COMPLETE";
    case ProbeStage::kHotRun:
      return "HOT_RUN";
    case ProbeStage::kHotSummary:
      return "HOT_SUMMARY";
    case ProbeStage::kDone:
      return "DONE";
    default:
      return "INVALID";
  }
}

// ---------------------------------------------------------------------------
// Descending parameter search
// ---------------------------------------------------------------------------
//
// The primary table is walked from the most conservative value downwards and
// the search keeps the lowest value that still passed. If even the first
// primary value fails, the extended table is walked upwards and the first value
// that passes wins. The whole cursor is a small POD so the firmware can hold it
// in RTC memory across deep sleeps.

struct ParamSearchState {
  std::uint8_t index{0};
  std::uint8_t extended{0};
  std::uint8_t have_best{0};
  std::uint8_t finished{0};
  std::uint16_t best{0};
};

struct ParamSearchTable {
  std::uint16_t const* primary{nullptr};
  std::uint8_t primary_count{0};
  std::uint16_t const* extended{nullptr};
  std::uint8_t extended_count{0};
};

// Product PRE search: 100 → 0, extended upwards to 200 then 300. This
// deliberately differs from AE_WIFI_PROBE_PRE_DELAYS_MS, which starts at 300.
inline ParamSearchTable ProductPreTable() {
  static constexpr std::uint16_t kPrimary[] = {100, 50, 25, 10, 0};
  static constexpr std::uint16_t kExtended[] = {200, 300};
  return ParamSearchTable{kPrimary, 5, kExtended, 2};
}

inline ParamSearchTable ProductPostTable() {
  static constexpr std::uint16_t kPrimary[] = {100, 50, 25, 10, 0};
  static constexpr std::uint16_t kExtended[] = {200, 300};
  return ParamSearchTable{kPrimary, 5, kExtended, 2};
}

inline bool ParamSearchFinished(ParamSearchState const& state) {
  return state.finished != 0;
}

inline bool ParamSearchFailed(ParamSearchState const& state) {
  return state.finished != 0 && state.have_best == 0;
}

inline std::uint16_t ParamSearchSelected(ParamSearchState const& state) {
  return state.best;
}

inline std::uint16_t ParamSearchCurrent(ParamSearchTable const& table,
                                        ParamSearchState const& state) {
  if (state.finished != 0) {
    return state.best;
  }
  if (state.extended == 0) {
    if (state.index < table.primary_count) {
      return table.primary[state.index];
    }
    return 0;
  }
  if (state.index < table.extended_count) {
    return table.extended[state.index];
  }
  return 0;
}

inline void ParamSearchRecord(ParamSearchTable const& table,
                              ParamSearchState& state, bool passed) {
  if (state.finished != 0) {
    return;
  }
  auto const current = ParamSearchCurrent(table, state);
  if (state.extended == 0) {
    if (passed) {
      state.best = current;
      state.have_best = 1;
      ++state.index;
      if (state.index >= table.primary_count) {
        state.finished = 1;
      }
      return;
    }
    if (state.have_best != 0) {
      state.finished = 1;
      return;
    }
    // Even the most conservative primary value failed: search upwards.
    state.extended = 1;
    state.index = 0;
    if (table.extended_count == 0) {
      state.finished = 1;
    }
    return;
  }
  if (passed) {
    state.best = current;
    state.have_best = 1;
    state.finished = 1;
    return;
  }
  ++state.index;
  if (state.index >= table.extended_count) {
    state.finished = 1;
  }
}

// ---------------------------------------------------------------------------
// ICMP profile trial and selection
// ---------------------------------------------------------------------------

struct IcmpTrial {
  std::uint16_t connects{0};
  std::uint16_t connect_ok{0};
  std::uint16_t icmp_sent{0};
  std::uint16_t icmp_recv{0};
  std::uint32_t connect_ms_total{0};
};

inline void IcmpTrialAddConnect(IcmpTrial& trial, bool connected,
                               std::uint32_t connect_ms,
                               std::uint16_t icmp_sent,
                               std::uint16_t icmp_recv) {
  ++trial.connects;
  if (!connected) {
    return;
  }
  ++trial.connect_ok;
  trial.connect_ms_total += connect_ms;
  trial.icmp_sent = static_cast<std::uint16_t>(trial.icmp_sent + icmp_sent);
  trial.icmp_recv = static_cast<std::uint16_t>(trial.icmp_recv + icmp_recv);
}

inline std::uint32_t IcmpTrialConnectMeanMs(IcmpTrial const& trial) {
  if (trial.connect_ok == 0) {
    return 0xFFFFFFFFu;
  }
  return trial.connect_ms_total / trial.connect_ok;
}

// Loss expressed in parts per thousand so comparisons stay integral.
inline std::uint32_t IcmpTrialLossPpt(IcmpTrial const& trial) {
  if (trial.icmp_sent == 0) {
    return 1000;
  }
  auto const lost =
      static_cast<std::uint32_t>(trial.icmp_sent - trial.icmp_recv);
  return (lost * 1000u) / static_cast<std::uint32_t>(trial.icmp_sent);
}

inline bool IcmpTrialPasses(
    IcmpTrial const& trial,
    std::uint16_t accept_num = AE_PRODUCT_PROBE_ICMP_ACCEPT_NUM,
    std::uint16_t accept_den = AE_PRODUCT_PROBE_ICMP_ACCEPT_DEN) {
  if (trial.connects == 0 || trial.connect_ok != trial.connects) {
    return false;
  }
  if (trial.icmp_sent == 0 || accept_den == 0) {
    return false;
  }
  return static_cast<std::uint32_t>(trial.icmp_recv) * accept_den >=
         static_cast<std::uint32_t>(trial.icmp_sent) * accept_num;
}

// Anything short of a clean sweep deserves the extra reconnects, provided at
// least one connect worked at all.
inline bool IcmpTrialBorderline(IcmpTrial const& trial) {
  if (trial.connects == 0 || trial.connect_ok == 0) {
    return false;
  }
  return trial.connect_ok != trial.connects ||
         trial.icmp_recv != trial.icmp_sent;
}

struct IcmpCandidate {
  std::uint8_t profile{0};
  bool measured{false};
  IcmpTrial trial{};
};

// Reliability first (ICMP loss, then connect failures), then connect time, then
// the richer profile. Returns the index of the winner or -1 when none passed.
inline int SelectIcmpProfile(IcmpCandidate const* candidates,
                             std::size_t count) {
  int best = -1;
  for (std::size_t i = 0; i < count; ++i) {
    auto const& c = candidates[i];
    if (!c.measured || !IcmpTrialPasses(c.trial)) {
      continue;
    }
    if (best < 0) {
      best = static_cast<int>(i);
      continue;
    }
    auto const& b = candidates[static_cast<std::size_t>(best)];
    auto const c_loss = IcmpTrialLossPpt(c.trial);
    auto const b_loss = IcmpTrialLossPpt(b.trial);
    if (c_loss != b_loss) {
      if (c_loss < b_loss) {
        best = static_cast<int>(i);
      }
      continue;
    }
    auto const c_fail =
        static_cast<std::uint16_t>(c.trial.connects - c.trial.connect_ok);
    auto const b_fail =
        static_cast<std::uint16_t>(b.trial.connects - b.trial.connect_ok);
    if (c_fail != b_fail) {
      if (c_fail < b_fail) {
        best = static_cast<int>(i);
      }
      continue;
    }
    auto const c_ms = IcmpTrialConnectMeanMs(c.trial);
    auto const b_ms = IcmpTrialConnectMeanMs(b.trial);
    if (c_ms != b_ms) {
      if (c_ms < b_ms) {
        best = static_cast<int>(i);
      }
      continue;
    }
    if (c.profile > b.profile) {
      best = static_cast<int>(i);
    }
  }
  return best;
}

// ---------------------------------------------------------------------------
// Batch verdict
// ---------------------------------------------------------------------------

enum class BatchVerdict : std::uint8_t {
  kPass = 0,
  kExtraBatch = 1,
  kFail = 2,
};

// A full batch passes. A near miss within the tolerance buys exactly one
// additional batch; a second near miss rejects the parameter.
inline BatchVerdict JudgeBatch(std::uint16_t unique, std::uint16_t expected,
                              bool extra_batch_already_used,
                              std::uint16_t tolerance = kProbeBatchTolerance) {
  if (expected == 0) {
    return BatchVerdict::kFail;
  }
  if (unique >= expected) {
    return BatchVerdict::kPass;
  }
  auto const missing = static_cast<std::uint16_t>(expected - unique);
  if (missing <= tolerance && !extra_batch_already_used) {
    return BatchVerdict::kExtraBatch;
  }
  return BatchVerdict::kFail;
}

// ---------------------------------------------------------------------------
// Late-arrival query
// ---------------------------------------------------------------------------

enum class LateQueryAction : std::uint8_t {
  kQueryAgain = 0,
  kAccept = 1,
  kTimeout = 2,
};

struct LateQueryState {
  std::uint32_t start_ms{0};
  std::uint32_t last_change_ms{0};
  std::uint16_t expected{0};
  std::uint16_t last_unique{0};
  std::uint8_t have_sample{0};
};

inline void LateQueryStart(LateQueryState& state, std::uint32_t now_ms,
                          std::uint16_t expected) {
  state = LateQueryState{};
  state.start_ms = now_ms;
  state.last_change_ms = now_ms;
  state.expected = expected;
}

inline LateQueryAction LateQueryOnResult(
    LateQueryState& state, std::uint32_t now_ms, std::uint16_t unique,
    std::uint32_t stable_ms = kProbeQueryStableMs,
    std::uint32_t timeout_ms = kProbeQueryTimeoutMs) {
  bool const changed = state.have_sample == 0 || unique != state.last_unique;
  if (changed) {
    state.last_change_ms = now_ms;
  }
  state.last_unique = unique;
  state.have_sample = 1;

  if (unique >= state.expected) {
    return LateQueryAction::kAccept;
  }
  if (!changed && (now_ms - state.last_change_ms) >= stable_ms) {
    return LateQueryAction::kAccept;
  }
  if ((now_ms - state.start_ms) >= timeout_ms) {
    return LateQueryAction::kTimeout;
  }
  return LateQueryAction::kQueryAgain;
}

// No answer arrived: only the overall timeout can end the wait.
inline LateQueryAction LateQueryOnTick(
    LateQueryState const& state, std::uint32_t now_ms,
    std::uint32_t timeout_ms = kProbeQueryTimeoutMs) {
  if ((now_ms - state.start_ms) >= timeout_ms) {
    return LateQueryAction::kTimeout;
  }
  return LateQueryAction::kQueryAgain;
}

// ---------------------------------------------------------------------------
// RTC state
// ---------------------------------------------------------------------------
//
// Deliberately minimal: no magic word, no version, no CRC and no network
// fingerprint. A cold boot is detected from the reset/wake reason instead, and
// any inconsistency is resolved by restarting the probe at stage 0.

struct ProbeRtcState {
  std::uint8_t stage{0};
  std::uint8_t profile{0};
  std::uint8_t reprobe_count{0};
  std::uint8_t extra_batch_used{0};

  std::uint16_t pre_ms{0};
  std::uint16_t post_ms{0};
  std::uint16_t sleep_ms{0};

  std::uint32_t session{0};
  std::uint16_t batch_id{0};
  std::uint16_t parameter_id{0};
  std::uint16_t seq{0};
  std::uint16_t batch_sent{0};
  std::uint16_t batch_expected{0};
  std::uint16_t hot_sent{0};
  std::uint16_t hot_fail{0};

  ParamSearchState pre_search{};
  ParamSearchState post_search{};

  std::uint16_t prev_seq{0};
  std::uint32_t prev_connect_us{0};
  std::uint32_t prev_cycle_us{0};
  std::uint32_t prev_txdone_us{0};
  std::uint32_t prev_sleep_elapsed_us{0};
  std::uint8_t prev_status{0};
  std::uint8_t prev_valid{0};
};

inline void ProductProbeColdBootReset(ProbeRtcState& state,
                                     std::uint32_t session) {
  state = ProbeRtcState{};
  state.stage = static_cast<std::uint8_t>(ProbeStage::kIcmpSelect);
  state.session = session;
  state.sleep_ms = kProbeSleepMs;
  state.batch_expected = kProbeBatchSize;
}

inline ProbeStage ProductProbeStage(ProbeRtcState const& state) {
  if (state.stage >= kProbeStageCount) {
    return ProbeStage::kIcmpSelect;
  }
  return static_cast<ProbeStage>(state.stage);
}

inline void ProductProbeAdvanceStage(ProbeRtcState& state) {
  if (state.stage < static_cast<std::uint8_t>(ProbeStage::kDone)) {
    ++state.stage;
  }
}

inline void ProductProbeSetStage(ProbeRtcState& state, ProbeStage stage) {
  state.stage = static_cast<std::uint8_t>(stage);
}

// Any pre-Encode failure on the hot path restarts the whole probe: the network
// may have changed, so the previously selected parameters are no longer valid.
inline void ProductProbeFailureResetStage(ProbeRtcState& state) {
  state.stage = static_cast<std::uint8_t>(ProbeStage::kIcmpSelect);
  if (state.reprobe_count < 0xFFu) {
    ++state.reprobe_count;
  }
  state.pre_search = ParamSearchState{};
  state.post_search = ParamSearchState{};
  state.extra_batch_used = 0;
  state.batch_sent = 0;
  state.batch_expected = kProbeBatchSize;
  state.prev_valid = 0;
  state.prev_seq = 0;
  state.prev_connect_us = 0;
  state.prev_cycle_us = 0;
  state.prev_txdone_us = 0;
  state.prev_sleep_elapsed_us = 0;
  state.prev_status = 0;
}

inline std::uint16_t ProductProbeNextSeq(ProbeRtcState& state) {
  ++state.seq;
  return state.seq;
}

inline void ProductProbeBeginBatch(ProbeRtcState& state,
                                  std::uint16_t parameter_id,
                                  std::uint16_t expected) {
  ++state.batch_id;
  state.parameter_id = parameter_id;
  state.batch_expected = expected;
  state.batch_sent = 0;
}

// Records the timing of the send that just completed. It travels in the *next*
// message because the current send's timing is only known afterwards.
inline void ProductProbeRecordHotSend(ProbeRtcState& state, std::uint16_t seq,
                                     std::uint32_t connect_us,
                                     std::uint32_t cycle_us,
                                     std::uint32_t txdone_us,
                                     std::uint32_t sleep_elapsed_us,
                                     std::uint8_t status) {
  state.prev_seq = seq;
  state.prev_connect_us = connect_us;
  state.prev_cycle_us = cycle_us;
  state.prev_txdone_us = txdone_us;
  state.prev_sleep_elapsed_us = sleep_elapsed_us;
  state.prev_status = status;
  state.prev_valid = 1;
  if (state.hot_sent < 0xFFFFu) {
    ++state.hot_sent;
  }
  if (state.batch_sent < 0xFFFFu) {
    ++state.batch_sent;
  }
}

// A batch is only worth querying once every packet of it has been sent.
inline bool ProductProbeShouldQueryBatch(ProbeRtcState const& state) {
  return state.batch_expected != 0 && state.batch_sent >= state.batch_expected;
}

inline void ProductProbeRecordHotFailure(ProbeRtcState& state) {
  if (state.hot_fail < 0xFFFFu) {
    ++state.hot_fail;
  }
}

}  // namespace ae::probe

#endif  // EXAMPLES_PROBE_RECEIVER_PRODUCT_PROBE_SELECT_H_
