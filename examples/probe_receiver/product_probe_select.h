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
// additional independent batch instead of rejecting the parameter outright.
#ifndef AE_PRODUCT_PROBE_BATCH_SIZE
#  define AE_PRODUCT_PROBE_BATCH_SIZE 20
#endif
#ifndef AE_PRODUCT_PROBE_BATCH_TOLERANCE
#  define AE_PRODUCT_PROBE_BATCH_TOLERANCE 1
#endif
// Late-arrival query: stop re-querying once the unique count has not moved for
// this long, and give up entirely after the timeout. Both budgets have to cover
// a full round trip through the cloud - query out, result back - which is far
// slower than a local exchange, and the last packets of a batch may still be in
// flight when the first query is asked.
#ifndef AE_PRODUCT_PROBE_QUERY_STABLE_MS
#  define AE_PRODUCT_PROBE_QUERY_STABLE_MS 3000
#endif
#ifndef AE_PRODUCT_PROBE_QUERY_TIMEOUT_MS
#  define AE_PRODUCT_PROBE_QUERY_TIMEOUT_MS 20000
#endif
// A query that is never answered has to be asked again: the request is one
// message and nothing retransmits it for us.
#ifndef AE_PRODUCT_PROBE_QUERY_RETRY_MS
#  define AE_PRODUCT_PROBE_QUERY_RETRY_MS 4000
#endif
// Production hot run length and the deep sleep between every measured send.
#ifndef AE_PRODUCT_PROBE_HOT_COUNT
#  define AE_PRODUCT_PROBE_HOT_COUNT 100
#endif
#ifndef AE_PRODUCT_PROBE_SLEEP_MS
#  define AE_PRODUCT_PROBE_SLEEP_MS 250
#endif
// A batch whose deep sleep could not be confirmed is thrown away and measured
// again. Repeating that indefinitely would hide a broken sleep path.
#ifndef AE_PRODUCT_PROBE_MAX_BATCH_INVALIDATIONS
#  define AE_PRODUCT_PROBE_MAX_BATCH_INVALIDATIONS 3
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
static constexpr std::uint32_t kProbeQueryRetryMs =
    AE_PRODUCT_PROBE_QUERY_RETRY_MS;
static constexpr std::uint16_t kProbeHotCount = AE_PRODUCT_PROBE_HOT_COUNT;
static constexpr std::uint16_t kProbeSleepMs = AE_PRODUCT_PROBE_SLEEP_MS;
static constexpr std::uint8_t kProbeMaxBatchInvalidations =
    AE_PRODUCT_PROBE_MAX_BATCH_INVALIDATIONS;

// Stages of one full self-configuration run. A cold boot always restarts at
// kIcmpSelect; every other transition is a single step forward.
//
// The two measured stages send exactly one datagram per wake and then enter a
// real timer deep sleep. There is no no-sleep probe stage: a POST delay is only
// ever accepted from packets that were measured the way production sends them.
enum class ProbeStage : std::uint8_t {
  kIcmpSelect = 0,
  kFullPreparePostBatch = 1,
  kPostProbeSleep250 = 2,
  kPostQuery = 3,
  kHotPrepare = 4,
  kPpkArm = 5,
  kHotRun = 6,
  kHotSummary = 7,
  kDone = 8,
};

static constexpr std::uint8_t kProbeStageCount = 9;

inline char const* ProbeStageName(ProbeStage stage) {
  switch (stage) {
    case ProbeStage::kIcmpSelect:
      return "ICMP_SELECT";
    case ProbeStage::kFullPreparePostBatch:
      return "FULL_PREPARE_POST_BATCH";
    case ProbeStage::kPostProbeSleep250:
      return "POST_PROBE_SLEEP250";
    case ProbeStage::kPostQuery:
      return "POST_QUERY";
    case ProbeStage::kHotPrepare:
      return "HOT_PREPARE";
    case ProbeStage::kPpkArm:
      return "PPK_ARM";
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

// Sends of these stages are measured and must stay silent, use the prepared hot
// path and end in a real deep sleep.
inline bool ProbeStageIsMeasured(ProbeStage stage) {
  return stage == ProbeStage::kPostProbeSleep250 ||
         stage == ProbeStage::kHotRun;
}

// ---------------------------------------------------------------------------
// PRE search: descending with an upward extension
// ---------------------------------------------------------------------------
//
// Only the ICMP stage uses this. The table is walked from the most conservative
// primary value downwards and the search keeps the lowest value that still
// passed. If even the first primary value fails, the extended table is walked
// upwards and the first value that passes wins. The cursor is a small POD so
// the firmware can hold it in RTC memory across deep sleeps.

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

// The whole PRE ladder, conservative last, as one ascending sequence.
//
// ICMP picks the profile and a first PRE, but an echo request is not what the
// product sends: the ping stack resolves and retries, so it survives delays the
// single prepared datagram after a fresh association does not. When the most
// conservative POST already fails, the PRE the ICMP stage chose is the thing
// under suspicion, and the next larger one is measured the same way every POST
// candidate is. Nothing here turns a failed batch into a pass; it only decides
// what is measured next.
inline std::uint16_t ProductPreEscalate(std::uint16_t current) {
  static constexpr std::uint16_t kLadder[] = {0, 10, 25, 50, 100, 200, 300};
  for (auto const step : kLadder) {
    if (step > current) {
      return step;
    }
  }
  return 0;
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
// POST search: one strictly descending table, no invented pass
// ---------------------------------------------------------------------------
//
// The POST delay is measured with production sends only: one datagram per wake,
// a real deep sleep in between. The table is walked conservative → fast and the
// search keeps the smallest value that actually delivered a full batch. There
// is no upward extension: if the most conservative value already fails, the
// whole path is declared invalid rather than assigning a value nothing
// supports.

inline std::uint16_t const* ProductPostTable(std::uint8_t& count) {
  static constexpr std::uint16_t kTable[] = {300, 200, 100, 50, 25, 10, 0};
  count = 7;
  return kTable;
}

inline std::uint8_t ProductPostTableCount() {
  std::uint8_t count = 0;
  (void)ProductPostTable(count);
  return count;
}

inline std::uint16_t ProductPostTableValue(std::uint8_t index) {
  std::uint8_t count = 0;
  auto const* table = ProductPostTable(count);
  return index < count ? table[index] : table[count - 1];
}

// Everything one batch contributes to the verdict. `local_ok` counts the sends
// that were locally clean: the socket accepted the whole datagram, the Wi-Fi
// TX-done callback reported success for that datagram, and the deep sleep that
// followed was confirmed by the next boot. `sleep_unconfirmed` means the batch
// cannot be judged at all and has to be measured again.
struct PostBatchStats {
  std::uint16_t expected{0};
  std::uint16_t unique{0};
  std::uint16_t local_ok{0};
  std::uint8_t sleep_unconfirmed{0};
};

// A packet only counts when the receiver saw it *and* the device considers its
// own send clean, so the weaker of the two counts decides.
inline std::uint16_t PostBatchEffective(PostBatchStats const& stats) {
  return stats.unique < stats.local_ok ? stats.unique : stats.local_ok;
}

enum class PostSearchAction : std::uint8_t {
  // Measure the current candidate with a fresh batch.
  kMeasureCandidate = 0,
  // Near miss: one more independent batch at the same candidate.
  kSecondBatch = 1,
  // The batch could not be judged; measure the same candidate again.
  kRetryBatch = 2,
  // Search over, PostSearchSelected() holds the answer.
  kFinishedSelected = 3,
  // Nothing passed, not even the most conservative candidate.
  kFinishedInvalid = 4,
};

struct PostSearchState {
  std::uint8_t index{0};
  std::uint8_t have_last_passed{0};
  std::uint8_t finished{0};
  std::uint8_t invalid{0};
  std::uint8_t second_batch{0};
  std::uint16_t last_passed{0};
  std::uint16_t first_unique{0};
  std::uint16_t first_expected{0};
};

inline bool PostSearchFinished(PostSearchState const& state) {
  return state.finished != 0;
}

// The path is invalid when even the most conservative POST delay failed. No hot
// run and no PPK capture may follow.
inline bool PostSearchInvalid(PostSearchState const& state) {
  return state.invalid != 0;
}

inline std::uint16_t PostSearchSelected(PostSearchState const& state) {
  return state.last_passed;
}

inline std::uint16_t PostSearchCurrent(PostSearchState const& state) {
  if (state.finished != 0) {
    return state.last_passed;
  }
  return ProductPostTableValue(state.index);
}

// Judges the batch that just finished and returns what to do next. The state
// machine is deliberately closed: no branch here can turn a failure into a
// selected value.
inline PostSearchAction PostSearchRecordBatch(
    PostSearchState& state, PostBatchStats const& stats,
    std::uint16_t tolerance = kProbeBatchTolerance) {
  if (state.finished != 0) {
    return state.invalid != 0 ? PostSearchAction::kFinishedInvalid
                              : PostSearchAction::kFinishedSelected;
  }
  if (stats.sleep_unconfirmed != 0 || stats.expected == 0) {
    state.second_batch = 0;
    state.first_unique = 0;
    state.first_expected = 0;
    return PostSearchAction::kRetryBatch;
  }

  auto const effective = PostBatchEffective(stats);
  bool passed = false;
  if (state.second_batch == 0) {
    if (effective >= stats.expected) {
      passed = true;
    } else if (static_cast<std::uint16_t>(stats.expected - effective) <=
               tolerance) {
      state.second_batch = 1;
      state.first_unique = effective;
      state.first_expected = stats.expected;
      return PostSearchAction::kSecondBatch;
    }
  } else {
    auto const total = static_cast<std::uint32_t>(state.first_unique) +
                       static_cast<std::uint32_t>(effective);
    auto const total_expected =
        static_cast<std::uint32_t>(state.first_expected) +
        static_cast<std::uint32_t>(stats.expected);
    // Two batches share one tolerance budget per batch: 38 of 40 at the default
    // batch size and tolerance.
    passed =
        (total + static_cast<std::uint32_t>(tolerance) * 2u) >= total_expected;
    state.second_batch = 0;
    state.first_unique = 0;
    state.first_expected = 0;
  }

  if (passed) {
    state.last_passed = PostSearchCurrent(state);
    state.have_last_passed = 1;
    ++state.index;
    if (state.index >= ProductPostTableCount()) {
      state.finished = 1;
      return PostSearchAction::kFinishedSelected;
    }
    return PostSearchAction::kMeasureCandidate;
  }

  state.finished = 1;
  if (state.have_last_passed != 0) {
    // A smaller value failed after a larger one passed: keep the larger one.
    return PostSearchAction::kFinishedSelected;
  }
  // The most conservative value failed with nothing behind it.
  state.invalid = 1;
  return PostSearchAction::kFinishedInvalid;
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
  std::uint32_t last_query_ms{0};
  std::uint16_t expected{0};
  std::uint16_t last_unique{0};
  std::uint8_t have_sample{0};
};

inline void LateQueryStart(LateQueryState& state, std::uint32_t now_ms,
                           std::uint16_t expected) {
  state = LateQueryState{};
  state.start_ms = now_ms;
  state.last_change_ms = now_ms;
  state.last_query_ms = now_ms;
  state.expected = expected;
}

// The caller asked the receiver again; the retry clock restarts from here.
inline void LateQueryMarkSent(LateQueryState& state, std::uint32_t now_ms) {
  state.last_query_ms = now_ms;
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

// Nothing has come back yet and the last request is older than the retry
// interval, so it is worth asking again rather than waiting out the timeout on
// a request that may never have arrived.
inline bool LateQueryRetryDue(LateQueryState const& state, std::uint32_t now_ms,
                              std::uint32_t retry_ms = kProbeQueryRetryMs) {
  return (now_ms - state.last_query_ms) >= retry_ms;
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
// One measured send
// ---------------------------------------------------------------------------

// Per-sample quality bits. A sample only counts towards a POST verdict when all
// of kSendtoOk, kTxDoneConfirmed and kSleepConfirmed are set.
enum class ProbeSampleFlag : std::uint8_t {
  kValid = 1u << 0,
  kSendtoOk = 1u << 1,
  kTxDoneConfirmed = 1u << 2,
  kSleepConfirmed = 1u << 3,
};

inline bool ProbeSampleHas(std::uint8_t flags, ProbeSampleFlag flag) {
  return (flags & static_cast<std::uint8_t>(flag)) != 0;
}

inline std::uint8_t ProbeSampleSet(std::uint8_t flags, ProbeSampleFlag flag,
                                   bool on) {
  auto const bit = static_cast<std::uint8_t>(flag);
  return on ? static_cast<std::uint8_t>(flags | bit)
            : static_cast<std::uint8_t>(flags &
                                        static_cast<std::uint8_t>(~bit));
}

inline bool ProbeSampleIsClean(std::uint8_t flags) {
  return ProbeSampleHas(flags, ProbeSampleFlag::kValid) &&
         ProbeSampleHas(flags, ProbeSampleFlag::kSendtoOk) &&
         ProbeSampleHas(flags, ProbeSampleFlag::kTxDoneConfirmed) &&
         ProbeSampleHas(flags, ProbeSampleFlag::kSleepConfirmed);
}

// Timing of one completed send. It travels in the *next* packet because a
// send's own cost is only known after it is over, and the sleep that follows it
// is only confirmed by the boot after that.
struct ProbeSendTiming {
  std::uint32_t connect_us{0};
  std::uint32_t cycle_us{0};
  std::uint32_t encode_us{0};
  std::uint32_t sendto_call_us{0};
  std::uint32_t send_to_txdone_us{0};
  std::int32_t txdone_minus_sendto_return_us{0};
  std::uint32_t actual_post_us{0};
  std::uint32_t teardown_us{0};
  std::uint32_t awake_us{0};
  std::uint32_t sleep_elapsed_us{0};
  std::uint32_t wake_overhead_us{0};
  std::uint16_t seq{0};
  std::uint8_t status{0};
  std::uint8_t flags{0};
};

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
  std::uint8_t batch_armed{0};

  std::uint8_t batch_invalidations{0};
  std::uint8_t pending_flags{0};
  std::uint16_t pending_seq{0};

  std::uint16_t pre_ms{0};
  std::uint16_t post_ms{0};
  std::uint16_t sleep_ms{0};
  std::uint16_t batch_id{0};

  std::uint32_t session{0};
  std::uint32_t send_generation{0};

  std::uint16_t parameter_id{0};
  std::uint16_t seq{0};
  std::uint16_t batch_sent{0};
  std::uint16_t batch_expected{0};
  std::uint16_t batch_local_ok{0};
  std::uint16_t hot_sent{0};
  std::uint16_t hot_fail{0};
  std::uint16_t hot_unconfirmed{0};

  ParamSearchState pre_search{};
  PostSearchState post_search{};

  ProbeSendTiming prev{};
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
  state.post_search = PostSearchState{};
  state.batch_armed = 0;
  state.batch_invalidations = 0;
  state.pending_flags = 0;
  state.pending_seq = 0;
  state.batch_sent = 0;
  state.batch_local_ok = 0;
  state.batch_expected = kProbeBatchSize;
  state.prev = ProbeSendTiming{};
}

inline std::uint16_t ProductProbeNextSeq(ProbeRtcState& state) {
  ++state.seq;
  return state.seq;
}

// Every send gets its own generation so a TX-done callback belonging to an
// earlier datagram can never be mistaken for this one's.
inline std::uint32_t ProductProbeNextGeneration(ProbeRtcState& state) {
  ++state.send_generation;
  return state.send_generation;
}

inline void ProductProbeBeginBatch(ProbeRtcState& state,
                                   std::uint16_t parameter_id,
                                   std::uint16_t expected) {
  ++state.batch_id;
  state.parameter_id = parameter_id;
  state.batch_expected = expected;
  state.batch_sent = 0;
  state.batch_local_ok = 0;
  state.batch_armed = 0;
  state.batch_invalidations = 0;
  state.pending_flags = 0;
  state.pending_seq = 0;
}

// The batch could not be measured the way production sends: throw it away and
// start a new, independently identified one at the same parameter. The
// invalidation count belongs to the parameter, not to the discarded batch.
inline void ProductProbeInvalidateBatch(ProbeRtcState& state) {
  auto const invalidations = state.batch_invalidations;
  ProductProbeBeginBatch(state, state.parameter_id, state.batch_expected);
  state.batch_invalidations = invalidations < 0xFFu
                                  ? static_cast<std::uint8_t>(invalidations + 1)
                                  : invalidations;
}

inline bool ProductProbeBatchInvalidationsExhausted(
    ProbeRtcState const& state) {
  return state.batch_invalidations >= kProbeMaxBatchInvalidations;
}

// Parks the send that just completed. It only becomes part of the batch once
// the following boot proves that a real timer deep sleep happened.
inline void ProductProbeParkSample(ProbeRtcState& state,
                                   ProbeSendTiming const& timing) {
  state.pending_seq = timing.seq;
  state.pending_flags =
      ProbeSampleSet(timing.flags, ProbeSampleFlag::kValid, true);
  state.prev = timing;
  state.prev.flags = state.pending_flags;
}

inline bool ProductProbeHasPendingSample(ProbeRtcState const& state) {
  return ProbeSampleHas(state.pending_flags, ProbeSampleFlag::kValid);
}

// The boot after a parked sample either confirms its deep sleep or rejects it.
// Returns true when the sample joined the batch.
inline bool ProductProbeCommitPendingSample(ProbeRtcState& state,
                                            bool sleep_confirmed) {
  if (!ProductProbeHasPendingSample(state)) {
    return false;
  }
  auto const flags = ProbeSampleSet(
      state.pending_flags, ProbeSampleFlag::kSleepConfirmed, sleep_confirmed);
  state.pending_flags = 0;
  state.pending_seq = 0;
  state.prev.flags = flags;
  if (!sleep_confirmed) {
    return false;
  }
  if (state.batch_sent < 0xFFFFu) {
    ++state.batch_sent;
  }
  if (ProbeSampleIsClean(flags)) {
    if (state.batch_local_ok < 0xFFFFu) {
      ++state.batch_local_ok;
    }
  } else if (state.hot_unconfirmed < 0xFFFFu) {
    ++state.hot_unconfirmed;
  }
  if (ProductProbeStage(state) == ProbeStage::kHotRun &&
      state.hot_sent < 0xFFFFu) {
    ++state.hot_sent;
  }
  return true;
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

// What the query stage feeds back into the POST search.
inline PostBatchStats ProductProbeBatchStats(ProbeRtcState const& state,
                                             std::uint16_t unique) {
  PostBatchStats stats{};
  stats.expected = state.batch_expected;
  stats.unique = unique;
  stats.local_ok = state.batch_local_ok;
  return stats;
}

}  // namespace ae::probe

#endif  // EXAMPLES_PROBE_RECEIVER_PRODUCT_PROBE_SELECT_H_
