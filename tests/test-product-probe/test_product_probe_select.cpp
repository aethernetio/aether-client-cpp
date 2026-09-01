/*
 * Copyright 2026 Aethernet Inc.
 *
 * Host unit tests for the product adaptive Wi-Fi probe selection algorithms:
 * stage machine, ICMP profile choice, PRE/POST search, batch verdicts, late
 * query handling and the previous-send timing carry.
 */

#include <unity.h>

#include "examples/probe_receiver/product_probe_select.h"

namespace ae::test_product_probe_select {

using probe::BatchVerdict;
using probe::IcmpCandidate;
using probe::IcmpTrial;
using probe::IcmpTrialBorderline;
using probe::IcmpTrialPasses;
using probe::JudgeBatch;
using probe::kProbeBatchSize;
using probe::kProbeQueryTimeoutMs;
using probe::kProbeSleepMs;
using probe::kProbeStageCount;
using probe::LateQueryAction;
using probe::LateQueryOnResult;
using probe::LateQueryOnTick;
using probe::LateQueryStart;
using probe::LateQueryState;
using probe::ParamSearchCurrent;
using probe::ParamSearchFailed;
using probe::ParamSearchFinished;
using probe::ParamSearchRecord;
using probe::ParamSearchSelected;
using probe::ParamSearchState;
using probe::ParamSearchTable;
using probe::ProbeRtcState;
using probe::ProbeStage;
using probe::ProbeStageName;
using probe::ProductPostTable;
using probe::ProductPreTable;
using probe::ProductProbeAdvanceStage;
using probe::ProductProbeBeginBatch;
using probe::ProductProbeColdBootReset;
using probe::ProductProbeFailureResetStage;
using probe::ProductProbeNextSeq;
using probe::ProductProbeRecordHotSend;
using probe::ProductProbeSetStage;
using probe::ProductProbeShouldQueryBatch;
using probe::ProductProbeStage;
using probe::SelectIcmpProfile;

namespace {

IcmpTrial MakeTrial(std::uint16_t connects, std::uint16_t connect_ok,
                    std::uint16_t icmp_sent, std::uint16_t icmp_recv,
                    std::uint32_t connect_ms_each) {
  IcmpTrial t{};
  t.connects = connects;
  t.connect_ok = connect_ok;
  t.icmp_sent = icmp_sent;
  t.icmp_recv = icmp_recv;
  t.connect_ms_total = connect_ms_each * connect_ok;
  return t;
}

// Walks a descending search, answering pass/fail from a caller-provided rule.
template <typename Rule>
ParamSearchState RunSearch(ParamSearchTable const& table, Rule rule) {
  ParamSearchState state{};
  for (int guard = 0; guard < 32 && !ParamSearchFinished(state); ++guard) {
    ParamSearchRecord(table, state, rule(ParamSearchCurrent(table, state)));
  }
  return state;
}

}  // namespace

// A cold boot must land on stage 0 with all selection progress cleared.
void test_ColdBootStartsAtStageZero() {
  ProbeRtcState state{};
  state.stage = static_cast<std::uint8_t>(ProbeStage::kHotRun);
  state.profile = 4;
  state.pre_ms = 25;
  state.seq = 77;
  state.prev_valid = 1;

  ProductProbeColdBootReset(state, 0xAABBCCDDu);

  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProbeStage::kIcmpSelect),
                          state.stage);
  TEST_ASSERT_EQUAL_UINT32(0xAABBCCDDu, state.session);
  TEST_ASSERT_EQUAL_UINT8(0, state.profile);
  TEST_ASSERT_EQUAL_UINT16(0, state.pre_ms);
  TEST_ASSERT_EQUAL_UINT16(0, state.seq);
  TEST_ASSERT_EQUAL_UINT8(0, state.prev_valid);
  TEST_ASSERT_EQUAL_UINT16(kProbeSleepMs, state.sleep_ms);
  TEST_ASSERT_EQUAL_UINT16(kProbeBatchSize, state.batch_expected);
}

// Stage advance walks 0..9 once and then saturates at kDone.
void test_StageAdvanceSaturatesAtDone() {
  ProbeRtcState state{};
  ProductProbeColdBootReset(state, 1);
  for (std::uint8_t expected = 1; expected < kProbeStageCount; ++expected) {
    ProductProbeAdvanceStage(state);
    TEST_ASSERT_EQUAL_UINT8(expected, state.stage);
  }
  ProductProbeAdvanceStage(state);
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProbeStage::kDone),
                          state.stage);
  TEST_ASSERT_EQUAL_STRING("DONE", ProbeStageName(ProductProbeStage(state)));
}

// Out-of-range stage bytes degrade to stage 0 rather than to garbage.
void test_InvalidStageByteReadsAsIcmpSelect() {
  ProbeRtcState state{};
  state.stage = 200;
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProbeStage::kIcmpSelect),
                          static_cast<std::uint8_t>(ProductProbeStage(state)));
}

// A trial with any connect failure never passes, whatever the ICMP ratio.
void test_IcmpTrialRequiresAllConnects() {
  TEST_ASSERT_TRUE(IcmpTrialPasses(MakeTrial(5, 5, 15, 15, 100)));
  TEST_ASSERT_FALSE(IcmpTrialPasses(MakeTrial(5, 4, 12, 12, 100)));
  // 13/15 replies is below the 9/10 threshold.
  TEST_ASSERT_FALSE(IcmpTrialPasses(MakeTrial(5, 5, 15, 13, 100)));
  // 14/15 replies clears it.
  TEST_ASSERT_TRUE(IcmpTrialPasses(MakeTrial(5, 5, 15, 14, 100)));
}

// Anything short of a clean sweep asks for the extra reconnects.
void test_IcmpBorderlineTriggersExtension() {
  TEST_ASSERT_FALSE(IcmpTrialBorderline(MakeTrial(5, 5, 15, 15, 100)));
  TEST_ASSERT_TRUE(IcmpTrialBorderline(MakeTrial(5, 5, 15, 14, 100)));
  TEST_ASSERT_TRUE(IcmpTrialBorderline(MakeTrial(5, 4, 12, 12, 100)));
  // Nothing connected at all: extending cannot help.
  TEST_ASSERT_FALSE(IcmpTrialBorderline(MakeTrial(5, 0, 0, 0, 0)));
}

// Reliability outranks connect time; connect time only breaks equal reliability.
void test_IcmpSelectionIsReliabilityFirst() {
  IcmpCandidate candidates[3]{};
  candidates[0].profile = 0;
  candidates[0].measured = true;
  candidates[0].trial = MakeTrial(5, 5, 15, 15, 400);
  candidates[1].profile = 3;
  candidates[1].measured = true;
  // Fast but lossy: still passes 9/10, must lose on reliability.
  candidates[1].trial = MakeTrial(5, 5, 20, 18, 90);
  candidates[2].profile = 4;
  candidates[2].measured = true;
  candidates[2].trial = MakeTrial(5, 5, 15, 15, 250);

  auto const winner = SelectIcmpProfile(candidates, 3);
  TEST_ASSERT_EQUAL_INT(2, winner);
  TEST_ASSERT_EQUAL_UINT8(4, candidates[winner].profile);
}

// With identical loss and connect time the richer profile wins.
void test_IcmpSelectionPrefersRicherProfileOnTie() {
  IcmpCandidate candidates[2]{};
  candidates[0].profile = 1;
  candidates[0].measured = true;
  candidates[0].trial = MakeTrial(5, 5, 15, 15, 120);
  candidates[1].profile = 3;
  candidates[1].measured = true;
  candidates[1].trial = MakeTrial(5, 5, 15, 15, 120);
  TEST_ASSERT_EQUAL_INT(1, SelectIcmpProfile(candidates, 2));
}

// No passing candidate must be reported as "no winner", not as profile 0.
void test_IcmpSelectionReportsNoWinner() {
  IcmpCandidate candidates[2]{};
  candidates[0].profile = 0;
  candidates[0].measured = true;
  candidates[0].trial = MakeTrial(5, 3, 9, 9, 300);
  candidates[1].profile = 2;
  candidates[1].measured = false;
  TEST_ASSERT_EQUAL_INT(-1, SelectIcmpProfile(candidates, 2));
}

// Product PRE table starts at 100 and descends, never at 300.
void test_PreSearchStartsAtHundred() {
  auto const table = ProductPreTable();
  ParamSearchState state{};
  TEST_ASSERT_EQUAL_UINT16(100, ParamSearchCurrent(table, state));
}

// Everything passes: the search keeps descending to the cheapest value.
void test_PreSearchSelectsLowestPassing() {
  auto const table = ProductPreTable();
  auto const state = RunSearch(table, [](std::uint16_t) { return true; });
  TEST_ASSERT_TRUE(ParamSearchFinished(state));
  TEST_ASSERT_FALSE(ParamSearchFailed(state));
  TEST_ASSERT_EQUAL_UINT16(0, ParamSearchSelected(state));
}

// Descending stops at the first failure and keeps the last value that worked.
void test_PreSearchStopsOnFirstFailure() {
  auto const table = ProductPreTable();
  auto const state =
      RunSearch(table, [](std::uint16_t pre) { return pre >= 25; });
  TEST_ASSERT_TRUE(ParamSearchFinished(state));
  TEST_ASSERT_FALSE(ParamSearchFailed(state));
  TEST_ASSERT_EQUAL_UINT16(25, ParamSearchSelected(state));
}

// When 100 already fails the search extends upwards to 200, then 300.
void test_PreSearchExtendsUpwards() {
  auto const table = ProductPreTable();
  auto const at_200 =
      RunSearch(table, [](std::uint16_t pre) { return pre >= 200; });
  TEST_ASSERT_TRUE(ParamSearchFinished(at_200));
  TEST_ASSERT_FALSE(ParamSearchFailed(at_200));
  TEST_ASSERT_EQUAL_UINT16(200, ParamSearchSelected(at_200));

  auto const at_300 =
      RunSearch(table, [](std::uint16_t pre) { return pre >= 300; });
  TEST_ASSERT_EQUAL_UINT16(300, ParamSearchSelected(at_300));

  auto const nothing = RunSearch(table, [](std::uint16_t) { return false; });
  TEST_ASSERT_TRUE(ParamSearchFinished(nothing));
  TEST_ASSERT_TRUE(ParamSearchFailed(nothing));
}

// The POST search follows the same table and semantics as PRE.
void test_PostSearchSelectsLowestPassing() {
  auto const table = ProductPostTable();
  auto const state =
      RunSearch(table, [](std::uint16_t post) { return post >= 50; });
  TEST_ASSERT_EQUAL_UINT16(50, ParamSearchSelected(state));
}

// The search cursor is a POD, so it survives a copy through RTC memory.
void test_ParamSearchCursorSurvivesRoundTrip() {
  auto const table = ProductPreTable();
  ParamSearchState state{};
  ParamSearchRecord(table, state, true);  // 100 passed
  ParamSearchState const saved = state;
  ParamSearchState restored = saved;
  TEST_ASSERT_EQUAL_UINT16(50, ParamSearchCurrent(table, restored));
  ParamSearchRecord(table, restored, false);
  TEST_ASSERT_TRUE(ParamSearchFinished(restored));
  TEST_ASSERT_EQUAL_UINT16(100, ParamSearchSelected(restored));
}

// 20/20 passes outright; 19/20 buys one extra batch; a second 19/20 fails.
void test_BatchVerdictTwentyOfTwenty() {
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(BatchVerdict::kPass),
                          static_cast<std::uint8_t>(JudgeBatch(20, 20, false)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(BatchVerdict::kExtraBatch),
                          static_cast<std::uint8_t>(JudgeBatch(19, 20, false)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(BatchVerdict::kFail),
                          static_cast<std::uint8_t>(JudgeBatch(19, 20, true)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(BatchVerdict::kFail),
                          static_cast<std::uint8_t>(JudgeBatch(18, 20, false)));
  // More than expected (a duplicate counted as unique elsewhere) still passes.
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(BatchVerdict::kPass),
                          static_cast<std::uint8_t>(JudgeBatch(21, 20, true)));
}

// A batch is only queried once all of its packets have been sent.
void test_QueryOnlyAfterWholeBatchSent() {
  ProbeRtcState state{};
  ProductProbeColdBootReset(state, 1);
  ProductProbeBeginBatch(state, 7, 20);
  TEST_ASSERT_FALSE(ProductProbeShouldQueryBatch(state));
  for (int i = 0; i < 19; ++i) {
    ProductProbeRecordHotSend(state, ProductProbeNextSeq(state), 0, 0, 0, 0, 0);
  }
  TEST_ASSERT_FALSE(ProductProbeShouldQueryBatch(state));
  ProductProbeRecordHotSend(state, ProductProbeNextSeq(state), 0, 0, 0, 0, 0);
  TEST_ASSERT_TRUE(ProductProbeShouldQueryBatch(state));
  TEST_ASSERT_EQUAL_UINT16(1, state.batch_id);
  TEST_ASSERT_EQUAL_UINT16(7, state.parameter_id);
}

// A complete count is accepted immediately without another round trip.
void test_LateQueryAcceptsCompleteCount() {
  LateQueryState q{};
  LateQueryStart(q, 1000, 20);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(LateQueryAction::kAccept),
      static_cast<std::uint8_t>(LateQueryOnResult(q, 1010, 20)));
}

// A packet still in flight makes the count move, so the probe queries again.
void test_LateQueryRetriesWhileCountMoves() {
  LateQueryState q{};
  LateQueryStart(q, 0, 20);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(LateQueryAction::kQueryAgain),
      static_cast<std::uint8_t>(LateQueryOnResult(q, 100, 18)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(LateQueryAction::kQueryAgain),
      static_cast<std::uint8_t>(LateQueryOnResult(q, 300, 19)));
  // The late packet finally landed.
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(LateQueryAction::kAccept),
      static_cast<std::uint8_t>(LateQueryOnResult(q, 500, 20)));
}

// An unchanged count for the stability window ends the wait short of timeout.
void test_LateQueryAcceptsStableCount() {
  LateQueryState q{};
  LateQueryStart(q, 0, 20);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(LateQueryAction::kQueryAgain),
      static_cast<std::uint8_t>(LateQueryOnResult(q, 100, 19)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(LateQueryAction::kQueryAgain),
      static_cast<std::uint8_t>(LateQueryOnResult(q, 400, 19)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(LateQueryAction::kAccept),
      static_cast<std::uint8_t>(LateQueryOnResult(q, 620, 19)));
}

// No answer at all still terminates on the overall timeout.
void test_LateQueryTimesOut() {
  LateQueryState q{};
  LateQueryStart(q, 0, 20);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(LateQueryAction::kQueryAgain),
      static_cast<std::uint8_t>(LateQueryOnTick(q, 1000)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(LateQueryAction::kTimeout),
      static_cast<std::uint8_t>(LateQueryOnTick(q, kProbeQueryTimeoutMs)));
}

// Send N's timing must be reported by send N+1, never by send N itself.
void test_PreviousTimingCarriesToNextSend() {
  ProbeRtcState state{};
  ProductProbeColdBootReset(state, 1);
  ProductProbeBeginBatch(state, 1, 20);

  auto const seq1 = ProductProbeNextSeq(state);
  TEST_ASSERT_EQUAL_UINT16(1, seq1);
  // Nothing to report on the very first send.
  TEST_ASSERT_EQUAL_UINT8(0, state.prev_valid);

  ProductProbeRecordHotSend(state, seq1, 1234, 5678, 90, 250123, 1);
  TEST_ASSERT_EQUAL_UINT8(1, state.prev_valid);
  TEST_ASSERT_EQUAL_UINT16(seq1, state.prev_seq);
  TEST_ASSERT_EQUAL_UINT32(1234, state.prev_connect_us);
  TEST_ASSERT_EQUAL_UINT32(5678, state.prev_cycle_us);

  auto const seq2 = ProductProbeNextSeq(state);
  TEST_ASSERT_EQUAL_UINT16(2, seq2);
  // Send 2 still carries send 1's numbers until it completes.
  TEST_ASSERT_EQUAL_UINT16(seq1, state.prev_seq);
  ProductProbeRecordHotSend(state, seq2, 4321, 8765, 45, 250456, 1);
  TEST_ASSERT_EQUAL_UINT16(seq2, state.prev_seq);
  TEST_ASSERT_EQUAL_UINT32(4321, state.prev_connect_us);
  TEST_ASSERT_EQUAL_UINT16(2, state.hot_sent);
}

// The hot sequence lives in RTC, so a deep sleep must not restart numbering.
void test_HotSequenceSurvivesDeepSleep() {
  ProbeRtcState state{};
  ProductProbeColdBootReset(state, 1);
  ProductProbeSetStage(state, ProbeStage::kHotRun);
  for (int i = 0; i < 5; ++i) {
    ProductProbeRecordHotSend(state, ProductProbeNextSeq(state), 0, 0, 0, 0, 0);
  }
  // A deep-sleep wake is only a copy of RTC memory into the new boot.
  ProbeRtcState const after_sleep = state;
  TEST_ASSERT_EQUAL_UINT16(5, after_sleep.seq);
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProbeStage::kHotRun),
                          after_sleep.stage);

  ProbeRtcState resumed = after_sleep;
  TEST_ASSERT_EQUAL_UINT16(6, ProductProbeNextSeq(resumed));
  TEST_ASSERT_EQUAL_UINT16(5, resumed.prev_seq);
}

// A pre-Encode Wi-Fi failure reprobes: stage 0, cleared searches, kept session.
void test_FailureResetsStageAndSearches() {
  ProbeRtcState state{};
  ProductProbeColdBootReset(state, 0xDEADBEEFu);
  ProductProbeSetStage(state, ProbeStage::kHotRun);
  state.profile = 4;
  state.pre_ms = 25;
  state.post_ms = 50;
  state.extra_batch_used = 1;
  ParamSearchRecord(ProductPreTable(), state.pre_search, true);
  ProductProbeRecordHotSend(state, ProductProbeNextSeq(state), 10, 20, 30, 40,
                            1);

  ProductProbeFailureResetStage(state);

  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProbeStage::kIcmpSelect),
                          state.stage);
  TEST_ASSERT_EQUAL_UINT8(1, state.reprobe_count);
  TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, state.session);
  TEST_ASSERT_EQUAL_UINT8(0, state.pre_search.have_best);
  TEST_ASSERT_EQUAL_UINT8(0, state.post_search.have_best);
  TEST_ASSERT_EQUAL_UINT8(0, state.extra_batch_used);
  TEST_ASSERT_EQUAL_UINT8(0, state.prev_valid);
  TEST_ASSERT_EQUAL_UINT16(0, state.batch_sent);
  // The hot sequence is a delivery identifier, not selection progress.
  TEST_ASSERT_EQUAL_UINT16(1, state.seq);
}

// Failures accumulate so a report can show how often the probe restarted.
void test_ReprobeCountAccumulates() {
  ProbeRtcState state{};
  ProductProbeColdBootReset(state, 1);
  ProductProbeFailureResetStage(state);
  ProductProbeFailureResetStage(state);
  ProductProbeFailureResetStage(state);
  TEST_ASSERT_EQUAL_UINT8(3, state.reprobe_count);
}

// The product RTC state must stay small and free of integrity fields.
// Pinned so the campaign report can quote the RTC cost without recompiling,
// and so growth is a deliberate change. The largest member is 4 bytes wide, so
// the layout is the same on the host and on the 32-bit target.
void test_RtcStateIsCompact() {
  TEST_ASSERT_EQUAL_UINT(64, sizeof(ProbeRtcState));
}

}  // namespace ae::test_product_probe_select

int test_product_probe_select() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_product_probe_select::test_ColdBootStartsAtStageZero);
  RUN_TEST(ae::test_product_probe_select::test_StageAdvanceSaturatesAtDone);
  RUN_TEST(ae::test_product_probe_select::test_InvalidStageByteReadsAsIcmpSelect);
  RUN_TEST(ae::test_product_probe_select::test_IcmpTrialRequiresAllConnects);
  RUN_TEST(ae::test_product_probe_select::test_IcmpBorderlineTriggersExtension);
  RUN_TEST(ae::test_product_probe_select::test_IcmpSelectionIsReliabilityFirst);
  RUN_TEST(
      ae::test_product_probe_select::test_IcmpSelectionPrefersRicherProfileOnTie);
  RUN_TEST(ae::test_product_probe_select::test_IcmpSelectionReportsNoWinner);
  RUN_TEST(ae::test_product_probe_select::test_PreSearchStartsAtHundred);
  RUN_TEST(ae::test_product_probe_select::test_PreSearchSelectsLowestPassing);
  RUN_TEST(ae::test_product_probe_select::test_PreSearchStopsOnFirstFailure);
  RUN_TEST(ae::test_product_probe_select::test_PreSearchExtendsUpwards);
  RUN_TEST(ae::test_product_probe_select::test_PostSearchSelectsLowestPassing);
  RUN_TEST(
      ae::test_product_probe_select::test_ParamSearchCursorSurvivesRoundTrip);
  RUN_TEST(ae::test_product_probe_select::test_BatchVerdictTwentyOfTwenty);
  RUN_TEST(ae::test_product_probe_select::test_QueryOnlyAfterWholeBatchSent);
  RUN_TEST(ae::test_product_probe_select::test_LateQueryAcceptsCompleteCount);
  RUN_TEST(ae::test_product_probe_select::test_LateQueryRetriesWhileCountMoves);
  RUN_TEST(ae::test_product_probe_select::test_LateQueryAcceptsStableCount);
  RUN_TEST(ae::test_product_probe_select::test_LateQueryTimesOut);
  RUN_TEST(ae::test_product_probe_select::test_PreviousTimingCarriesToNextSend);
  RUN_TEST(ae::test_product_probe_select::test_HotSequenceSurvivesDeepSleep);
  RUN_TEST(ae::test_product_probe_select::test_FailureResetsStageAndSearches);
  RUN_TEST(ae::test_product_probe_select::test_ReprobeCountAccumulates);
  RUN_TEST(ae::test_product_probe_select::test_RtcStateIsCompact);
  return UNITY_END();
}
