/*
 * Copyright 2026 Aethernet Inc.
 *
 * Host unit tests for the product adaptive Wi-Fi probe selection algorithms:
 * stage machine, ICMP profile choice, PRE search, the descending POST search
 * and its refusal to invent a passing value, deep-sleep sample accounting,
 * late query handling and the previous-send timing carry.
 */

#include <unity.h>

#include "examples/probe_receiver/product_probe_select.h"

namespace ae::test_product_probe_select {

using probe::IcmpCandidate;
using probe::IcmpTrial;
using probe::IcmpTrialBorderline;
using probe::IcmpTrialPasses;
using probe::kProbeBatchSize;
using probe::kProbeMaxBatchInvalidations;
using probe::kProbeQueryRetryMs;
using probe::kProbeQueryStableMs;
using probe::kProbeQueryTimeoutMs;
using probe::kProbeSleepMs;
using probe::kProbeStageCount;
using probe::LateQueryAction;
using probe::LateQueryMarkSent;
using probe::LateQueryOnResult;
using probe::LateQueryOnTick;
using probe::LateQueryRetryDue;
using probe::LateQueryStart;
using probe::LateQueryState;
using probe::ParamSearchCurrent;
using probe::ParamSearchFailed;
using probe::ParamSearchFinished;
using probe::ParamSearchRecord;
using probe::ParamSearchSelected;
using probe::ParamSearchState;
using probe::ParamSearchTable;
using probe::PostBatchEffective;
using probe::PostBatchStats;
using probe::PostSearchAction;
using probe::PostSearchCurrent;
using probe::PostSearchFinished;
using probe::PostSearchInvalid;
using probe::PostSearchRecordBatch;
using probe::PostSearchSelected;
using probe::PostSearchState;
using probe::ProbeRtcState;
using probe::ProbeSampleFlag;
using probe::ProbeSampleIsClean;
using probe::ProbeSampleSet;
using probe::ProbeSendTiming;
using probe::ProbeStage;
using probe::ProbeStageIsMeasured;
using probe::ProbeStageName;
using probe::ProductPostTableCount;
using probe::ProductPostTableValue;
using probe::ProductPreTable;
using probe::ProductProbeAdvanceStage;
using probe::ProductProbeBatchInvalidationsExhausted;
using probe::ProductProbeBatchStats;
using probe::ProductProbeBeginBatch;
using probe::ProductProbeColdBootReset;
using probe::ProductProbeCommitPendingSample;
using probe::ProductProbeFailureResetStage;
using probe::ProductProbeHasPendingSample;
using probe::ProductProbeInvalidateBatch;
using probe::ProductProbeNextGeneration;
using probe::ProductProbeNextSeq;
using probe::ProductProbeParkSample;
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

// Walks a descending PRE search, answering pass/fail from a caller rule.
template <typename Rule>
ParamSearchState RunSearch(ParamSearchTable const& table, Rule rule) {
  ParamSearchState state{};
  for (int guard = 0; guard < 32 && !ParamSearchFinished(state); ++guard) {
    ParamSearchRecord(table, state, rule(ParamSearchCurrent(table, state)));
  }
  return state;
}

// A batch where every packet arrived and every send was locally clean.
PostBatchStats FullBatch(std::uint16_t expected = kProbeBatchSize) {
  PostBatchStats stats{};
  stats.expected = expected;
  stats.unique = expected;
  stats.local_ok = expected;
  return stats;
}

PostBatchStats DeliveredBatch(std::uint16_t unique,
                              std::uint16_t expected = kProbeBatchSize) {
  PostBatchStats stats{};
  stats.expected = expected;
  stats.unique = unique;
  stats.local_ok = expected;
  return stats;
}

// Drives a POST search to its end, answering each candidate from a rule.
template <typename Rule>
PostSearchState RunPostSearch(Rule rule) {
  PostSearchState state{};
  for (int guard = 0; guard < 64 && !PostSearchFinished(state); ++guard) {
    auto const unique = rule(PostSearchCurrent(state));
    PostSearchRecordBatch(state, DeliveredBatch(unique));
  }
  return state;
}

// One completed send, as the firmware parks it before its deep sleep.
ProbeSendTiming CleanSample(std::uint16_t seq) {
  ProbeSendTiming timing{};
  timing.seq = seq;
  timing.status = 1;
  timing.flags = ProbeSampleSet(timing.flags, ProbeSampleFlag::kSendtoOk, true);
  timing.flags =
      ProbeSampleSet(timing.flags, ProbeSampleFlag::kTxDoneConfirmed, true);
  return timing;
}

// Sends one packet and lets the following boot confirm its deep sleep.
void SendAndConfirm(ProbeRtcState& state, bool sleep_confirmed = true) {
  ProductProbeParkSample(state, CleanSample(ProductProbeNextSeq(state)));
  ProductProbeCommitPendingSample(state, sleep_confirmed);
}

}  // namespace

// A cold boot must land on stage 0 with all selection progress cleared.
void test_ColdBootStartsAtStageZero() {
  ProbeRtcState state{};
  state.stage = static_cast<std::uint8_t>(ProbeStage::kHotRun);
  state.profile = 4;
  state.pre_ms = 25;
  state.seq = 77;
  state.pending_flags = 0xFF;

  ProductProbeColdBootReset(state, 0xAABBCCDDu);

  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProbeStage::kIcmpSelect),
                          state.stage);
  TEST_ASSERT_EQUAL_UINT32(0xAABBCCDDu, state.session);
  TEST_ASSERT_EQUAL_UINT8(0, state.profile);
  TEST_ASSERT_EQUAL_UINT16(0, state.pre_ms);
  TEST_ASSERT_EQUAL_UINT16(0, state.seq);
  TEST_ASSERT_EQUAL_UINT8(0, state.pending_flags);
  TEST_ASSERT_EQUAL_UINT16(kProbeSleepMs, state.sleep_ms);
  TEST_ASSERT_EQUAL_UINT16(kProbeBatchSize, state.batch_expected);
}

// Stage advance walks 0..8 once and then saturates at kDone.
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

// The stage list has exactly one POST probe stage and it is the sleeping one:
// the old no-sleep probe and the separate sleep confirmation are gone.
void test_StageNamesMatchSleepingProbeFlow() {
  TEST_ASSERT_EQUAL_UINT8(9, kProbeStageCount);
  TEST_ASSERT_EQUAL_STRING("ICMP_SELECT",
                           ProbeStageName(ProbeStage::kIcmpSelect));
  TEST_ASSERT_EQUAL_STRING("FULL_PREPARE_POST_BATCH",
                           ProbeStageName(ProbeStage::kFullPreparePostBatch));
  TEST_ASSERT_EQUAL_STRING("POST_PROBE_SLEEP250",
                           ProbeStageName(ProbeStage::kPostProbeSleep250));
  TEST_ASSERT_EQUAL_STRING("POST_QUERY",
                           ProbeStageName(ProbeStage::kPostQuery));
  TEST_ASSERT_EQUAL_STRING("HOT_PREPARE",
                           ProbeStageName(ProbeStage::kHotPrepare));
  TEST_ASSERT_EQUAL_STRING("PPK_ARM", ProbeStageName(ProbeStage::kPpkArm));
  TEST_ASSERT_EQUAL_STRING("HOT_RUN", ProbeStageName(ProbeStage::kHotRun));
  TEST_ASSERT_EQUAL_STRING("HOT_SUMMARY",
                           ProbeStageName(ProbeStage::kHotSummary));
}

// Only the two one-packet-per-wake stages are measured, so only they are
// silent and only they hand over with a real deep sleep.
void test_OnlySleepingStagesAreMeasured() {
  TEST_ASSERT_TRUE(ProbeStageIsMeasured(ProbeStage::kPostProbeSleep250));
  TEST_ASSERT_TRUE(ProbeStageIsMeasured(ProbeStage::kHotRun));
  TEST_ASSERT_FALSE(ProbeStageIsMeasured(ProbeStage::kIcmpSelect));
  TEST_ASSERT_FALSE(ProbeStageIsMeasured(ProbeStage::kFullPreparePostBatch));
  TEST_ASSERT_FALSE(ProbeStageIsMeasured(ProbeStage::kPostQuery));
  TEST_ASSERT_FALSE(ProbeStageIsMeasured(ProbeStage::kHotPrepare));
  // PPK_ARM has to print its marker, so it must not be a measured stage.
  TEST_ASSERT_FALSE(ProbeStageIsMeasured(ProbeStage::kPpkArm));
  TEST_ASSERT_FALSE(ProbeStageIsMeasured(ProbeStage::kHotSummary));
  TEST_ASSERT_FALSE(ProbeStageIsMeasured(ProbeStage::kDone));
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

// Reliability outranks connect time; connect time only breaks equal
// reliability.
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

// The ICMP-only PRE search may still extend upwards to 200, then 300.
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

// The POST table is one strictly descending ladder from the most conservative
// value to zero. There is no upward extension to fall back on.
void test_PostTableIsStrictlyDescending() {
  TEST_ASSERT_EQUAL_UINT(7, ProductPostTableCount());
  std::uint16_t const expected[] = {300, 200, 100, 50, 25, 10, 0};
  for (std::uint8_t i = 0; i < ProductPostTableCount(); ++i) {
    TEST_ASSERT_EQUAL_UINT16(expected[i], ProductPostTableValue(i));
  }
  PostSearchState state{};
  TEST_ASSERT_EQUAL_UINT16(300, PostSearchCurrent(state));
}

// A full batch of locally clean sends passes and moves on to a smaller value.
void test_PostFullBatchPassesAndDescends() {
  PostSearchState state{};
  auto const action = PostSearchRecordBatch(state, FullBatch());
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(PostSearchAction::kMeasureCandidate),
      static_cast<std::uint8_t>(action));
  TEST_ASSERT_EQUAL_UINT16(300, PostSearchSelected(state));
  TEST_ASSERT_EQUAL_UINT16(200, PostSearchCurrent(state));
}

// Everything delivers: the search bottoms out at POST 0.
void test_PostAllPassSelectsZero() {
  auto const state =
      RunPostSearch([](std::uint16_t) { return kProbeBatchSize; });
  TEST_ASSERT_TRUE(PostSearchFinished(state));
  TEST_ASSERT_FALSE(PostSearchInvalid(state));
  TEST_ASSERT_EQUAL_UINT16(0, PostSearchSelected(state));
}

// A smaller value failing after a larger one passed keeps the larger one.
void test_PostSmallerFailureKeepsLastPassed() {
  auto const state = RunPostSearch([](std::uint16_t post) -> std::uint16_t {
    return post >= 50 ? kProbeBatchSize : 10;
  });
  TEST_ASSERT_TRUE(PostSearchFinished(state));
  TEST_ASSERT_FALSE(PostSearchInvalid(state));
  TEST_ASSERT_EQUAL_UINT16(50, PostSearchSelected(state));
}

// Nothing delivers, not even the most conservative value: the path is invalid.
// It must never be resolved by assigning 300 anyway.
void test_PostTotalFailureIsInvalidPath() {
  auto const state =
      RunPostSearch([](std::uint16_t) -> std::uint16_t { return 0; });
  TEST_ASSERT_TRUE(PostSearchFinished(state));
  TEST_ASSERT_TRUE(PostSearchInvalid(state));
  TEST_ASSERT_EQUAL_UINT16(0, PostSearchSelected(state));
}

// 18 of 20 is a plain failure: no extra batch, no second chance.
void test_PostEighteenOfTwentyFails() {
  PostSearchState state{};
  auto const action = PostSearchRecordBatch(state, DeliveredBatch(18));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(PostSearchAction::kFinishedInvalid),
      static_cast<std::uint8_t>(action));
  TEST_ASSERT_TRUE(PostSearchInvalid(state));
}

// 19 of 20 buys exactly one more independent batch, and 19+19 of 40 clears the
// combined threshold.
void test_PostNearMissPassesOnAggregate() {
  PostSearchState state{};
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(PostSearchAction::kSecondBatch),
      static_cast<std::uint8_t>(
          PostSearchRecordBatch(state, DeliveredBatch(19))));
  TEST_ASSERT_EQUAL_UINT16(300, PostSearchCurrent(state));

  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(PostSearchAction::kMeasureCandidate),
      static_cast<std::uint8_t>(
          PostSearchRecordBatch(state, DeliveredBatch(19))));
  TEST_ASSERT_EQUAL_UINT16(300, PostSearchSelected(state));
  TEST_ASSERT_EQUAL_UINT16(200, PostSearchCurrent(state));
}

// 19 then 18 is 37 of 40, below the combined threshold, so the candidate fails.
void test_PostNearMissFailsOnAggregate() {
  PostSearchState state{};
  PostSearchRecordBatch(state, DeliveredBatch(19));
  auto const action = PostSearchRecordBatch(state, DeliveredBatch(18));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(PostSearchAction::kFinishedInvalid),
      static_cast<std::uint8_t>(action));
  TEST_ASSERT_TRUE(PostSearchInvalid(state));
}

// The near-miss allowance belongs to the candidate, not to the search: a later
// candidate gets its own second batch.
void test_PostSecondBatchAllowanceIsPerCandidate() {
  PostSearchState state{};
  PostSearchRecordBatch(state, FullBatch());  // 300 passes outright
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(PostSearchAction::kSecondBatch),
      static_cast<std::uint8_t>(
          PostSearchRecordBatch(state, DeliveredBatch(19))));
}

// The receiver saw everything but the device did not: a send whose TX-done was
// never confirmed cannot help a candidate pass.
void test_PostUnconfirmedSendBlocksPass() {
  PostBatchStats stats{};
  stats.expected = kProbeBatchSize;
  stats.unique = kProbeBatchSize;
  stats.local_ok = kProbeBatchSize - 2;
  TEST_ASSERT_EQUAL_UINT16(kProbeBatchSize - 2, PostBatchEffective(stats));

  PostSearchState state{};
  auto const action = PostSearchRecordBatch(state, stats);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(PostSearchAction::kFinishedInvalid),
      static_cast<std::uint8_t>(action));
}

// A batch whose deep sleep could not be confirmed is not a verdict at all: it
// is measured again rather than counted for or against the candidate.
void test_PostUnconfirmedSleepRetriesBatch() {
  PostSearchState state{};
  PostBatchStats stats = FullBatch();
  stats.sleep_unconfirmed = 1;
  auto const action = PostSearchRecordBatch(state, stats);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(PostSearchAction::kRetryBatch),
      static_cast<std::uint8_t>(action));
  TEST_ASSERT_FALSE(PostSearchFinished(state));
  TEST_ASSERT_EQUAL_UINT16(300, PostSearchCurrent(state));
}

// A retry after a near miss starts the candidate's evidence over, so the
// discarded batch cannot contribute to the aggregate.
void test_PostRetryClearsPendingAggregate() {
  PostSearchState state{};
  PostSearchRecordBatch(state, DeliveredBatch(19));
  PostBatchStats unconfirmed = FullBatch();
  unconfirmed.sleep_unconfirmed = 1;
  PostSearchRecordBatch(state, unconfirmed);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(PostSearchAction::kSecondBatch),
      static_cast<std::uint8_t>(
          PostSearchRecordBatch(state, DeliveredBatch(19))));
}

// A batch is only queried once every packet of it has been sent, and a packet
// only counts once its deep sleep was confirmed.
void test_QueryOnlyAfterWholeBatchSent() {
  ProbeRtcState state{};
  ProductProbeColdBootReset(state, 1);
  ProductProbeBeginBatch(state, 300, 20);
  TEST_ASSERT_FALSE(ProductProbeShouldQueryBatch(state));
  for (int i = 0; i < 19; ++i) {
    SendAndConfirm(state);
  }
  TEST_ASSERT_FALSE(ProductProbeShouldQueryBatch(state));
  SendAndConfirm(state);
  TEST_ASSERT_TRUE(ProductProbeShouldQueryBatch(state));
  TEST_ASSERT_EQUAL_UINT16(1, state.batch_id);
  TEST_ASSERT_EQUAL_UINT16(300, state.parameter_id);
  TEST_ASSERT_EQUAL_UINT16(20, state.batch_local_ok);
}

// A parked sample joins the batch only when the next boot confirms the sleep.
void test_PendingSampleNeedsConfirmedSleep() {
  ProbeRtcState state{};
  ProductProbeColdBootReset(state, 1);
  ProductProbeBeginBatch(state, 300, 20);

  ProductProbeParkSample(state, CleanSample(ProductProbeNextSeq(state)));
  TEST_ASSERT_TRUE(ProductProbeHasPendingSample(state));
  TEST_ASSERT_EQUAL_UINT16(0, state.batch_sent);

  TEST_ASSERT_TRUE(ProductProbeCommitPendingSample(state, true));
  TEST_ASSERT_FALSE(ProductProbeHasPendingSample(state));
  TEST_ASSERT_EQUAL_UINT16(1, state.batch_sent);
  TEST_ASSERT_EQUAL_UINT16(1, state.batch_local_ok);
  TEST_ASSERT_TRUE(ProbeSampleIsClean(state.prev.flags));
}

// A software restart instead of the deep sleep rejects the sample outright.
void test_UnconfirmedSleepRejectsSample() {
  ProbeRtcState state{};
  ProductProbeColdBootReset(state, 1);
  ProductProbeBeginBatch(state, 300, 20);

  ProductProbeParkSample(state, CleanSample(ProductProbeNextSeq(state)));
  TEST_ASSERT_FALSE(ProductProbeCommitPendingSample(state, false));
  TEST_ASSERT_EQUAL_UINT16(0, state.batch_sent);
  TEST_ASSERT_EQUAL_UINT16(0, state.batch_local_ok);
  TEST_ASSERT_FALSE(ProbeSampleIsClean(state.prev.flags));
}

// A send whose TX-done never succeeded still occupies its slot, because the
// prepared nonce is spent, but it is not a clean sample.
void test_UnconfirmedTxDoneOccupiesSlotWithoutCounting() {
  ProbeRtcState state{};
  ProductProbeColdBootReset(state, 1);
  ProductProbeBeginBatch(state, 300, 20);

  auto timing = CleanSample(ProductProbeNextSeq(state));
  timing.status = 2;
  timing.flags =
      ProbeSampleSet(timing.flags, ProbeSampleFlag::kTxDoneConfirmed, false);
  ProductProbeParkSample(state, timing);
  TEST_ASSERT_TRUE(ProductProbeCommitPendingSample(state, true));

  TEST_ASSERT_EQUAL_UINT16(1, state.batch_sent);
  TEST_ASSERT_EQUAL_UINT16(0, state.batch_local_ok);
  TEST_ASSERT_EQUAL_UINT16(1, state.hot_unconfirmed);

  auto const stats = ProductProbeBatchStats(state, 1);
  TEST_ASSERT_EQUAL_UINT16(0, PostBatchEffective(stats));
}

// Discarding a batch renumbers it so the receiver counts the retry separately,
// and the invalidation budget is bounded.
void test_InvalidatedBatchGetsNewIdAndIsBounded() {
  ProbeRtcState state{};
  ProductProbeColdBootReset(state, 1);
  ProductProbeBeginBatch(state, 300, 20);
  SendAndConfirm(state);
  TEST_ASSERT_EQUAL_UINT16(1, state.batch_sent);

  ProductProbeInvalidateBatch(state);
  TEST_ASSERT_EQUAL_UINT16(2, state.batch_id);
  TEST_ASSERT_EQUAL_UINT16(0, state.batch_sent);
  TEST_ASSERT_EQUAL_UINT16(0, state.batch_local_ok);
  TEST_ASSERT_EQUAL_UINT8(0, state.batch_armed);
  TEST_ASSERT_EQUAL_UINT8(1, state.batch_invalidations);
  TEST_ASSERT_FALSE(ProductProbeBatchInvalidationsExhausted(state));

  for (std::uint8_t i = 1; i < kProbeMaxBatchInvalidations; ++i) {
    ProductProbeInvalidateBatch(state);
  }
  TEST_ASSERT_TRUE(ProductProbeBatchInvalidationsExhausted(state));

  // Starting a genuinely new batch clears the budget again.
  ProductProbeBeginBatch(state, 200, 20);
  TEST_ASSERT_EQUAL_UINT8(0, state.batch_invalidations);
}

// Only the production stage feeds the hot counters the summary reports.
void test_HotCountersOnlyGrowDuringHotRun() {
  ProbeRtcState state{};
  ProductProbeColdBootReset(state, 1);
  ProductProbeSetStage(state, ProbeStage::kPostProbeSleep250);
  ProductProbeBeginBatch(state, 300, 20);
  SendAndConfirm(state);
  TEST_ASSERT_EQUAL_UINT16(0, state.hot_sent);

  ProductProbeSetStage(state, ProbeStage::kHotRun);
  SendAndConfirm(state);
  TEST_ASSERT_EQUAL_UINT16(1, state.hot_sent);
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
// Expressed in terms of the window itself, so retuning the budget for a slower
// network does not silently change what this test checks.
void test_LateQueryAcceptsStableCount() {
  auto const first = std::uint32_t{100};
  LateQueryState q{};
  LateQueryStart(q, 0, 20);
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(LateQueryAction::kQueryAgain),
      static_cast<std::uint8_t>(LateQueryOnResult(q, first, 19)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<std::uint8_t>(LateQueryAction::kQueryAgain),
      static_cast<std::uint8_t>(
          LateQueryOnResult(q, first + kProbeQueryStableMs - 1, 19)));
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LateQueryAction::kAccept),
                          static_cast<std::uint8_t>(LateQueryOnResult(
                              q, first + kProbeQueryStableMs, 19)));
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

// The query budget has to cover a round trip through the cloud. A budget
// shorter than that made every batch time out at nought delivered, which the
// old search then papered over by assigning a POST delay anyway.
void test_LateQueryBudgetCoversACloudRoundTrip() {
  TEST_ASSERT_TRUE(kProbeQueryTimeoutMs >= 10000);
  TEST_ASSERT_TRUE(kProbeQueryStableMs >= 1000);
  TEST_ASSERT_TRUE(kProbeQueryRetryMs < kProbeQueryTimeoutMs);
}

// Nothing retransmits the query itself, so an unanswered one is asked again
// instead of consuming the whole budget.
void test_LateQueryAsksAgainWhenUnanswered() {
  LateQueryState q{};
  LateQueryStart(q, 1000, 20);
  TEST_ASSERT_FALSE(LateQueryRetryDue(q, 1000 + kProbeQueryRetryMs - 1));
  TEST_ASSERT_TRUE(LateQueryRetryDue(q, 1000 + kProbeQueryRetryMs));

  LateQueryMarkSent(q, 1000 + kProbeQueryRetryMs);
  TEST_ASSERT_FALSE(LateQueryRetryDue(q, 1000 + kProbeQueryRetryMs));
  TEST_ASSERT_TRUE(LateQueryRetryDue(q, 1000 + 2 * kProbeQueryRetryMs));

  // Re-asking must not extend the overall deadline.
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(LateQueryAction::kTimeout),
                          static_cast<std::uint8_t>(
                              LateQueryOnTick(q, 1000 + kProbeQueryTimeoutMs)));
}

// Send N's timing must be reported by send N+1, never by send N itself.
void test_PreviousTimingCarriesToNextSend() {
  ProbeRtcState state{};
  ProductProbeColdBootReset(state, 1);
  ProductProbeBeginBatch(state, 300, 20);

  auto const seq1 = ProductProbeNextSeq(state);
  TEST_ASSERT_EQUAL_UINT16(1, seq1);
  // Nothing to report on the very first send.
  TEST_ASSERT_EQUAL_UINT8(0, state.prev.flags);

  auto first = CleanSample(seq1);
  first.connect_us = 1234;
  first.cycle_us = 5678;
  first.actual_post_us = 25100;
  first.txdone_minus_sendto_return_us = -80;
  ProductProbeParkSample(state, first);
  ProductProbeCommitPendingSample(state, true);

  TEST_ASSERT_EQUAL_UINT16(seq1, state.prev.seq);
  TEST_ASSERT_EQUAL_UINT32(1234, state.prev.connect_us);
  TEST_ASSERT_EQUAL_UINT32(5678, state.prev.cycle_us);
  TEST_ASSERT_EQUAL_UINT32(25100, state.prev.actual_post_us);
  TEST_ASSERT_EQUAL_INT32(-80, state.prev.txdone_minus_sendto_return_us);

  auto const seq2 = ProductProbeNextSeq(state);
  TEST_ASSERT_EQUAL_UINT16(2, seq2);
  // Send 2 still carries send 1's numbers until it completes.
  TEST_ASSERT_EQUAL_UINT16(seq1, state.prev.seq);

  auto second = CleanSample(seq2);
  second.connect_us = 4321;
  ProductProbeParkSample(state, second);
  ProductProbeCommitPendingSample(state, true);
  TEST_ASSERT_EQUAL_UINT16(seq2, state.prev.seq);
  TEST_ASSERT_EQUAL_UINT32(4321, state.prev.connect_us);
  TEST_ASSERT_EQUAL_UINT16(2, state.batch_sent);
}

// Every send gets its own generation, so a callback raised for an earlier
// datagram can never be credited to the current one.
void test_SendGenerationIsStrictlyIncreasing() {
  ProbeRtcState state{};
  ProductProbeColdBootReset(state, 1);
  TEST_ASSERT_EQUAL_UINT32(1, ProductProbeNextGeneration(state));
  TEST_ASSERT_EQUAL_UINT32(2, ProductProbeNextGeneration(state));
  ProbeRtcState const after_sleep = state;
  ProbeRtcState resumed = after_sleep;
  TEST_ASSERT_EQUAL_UINT32(3, ProductProbeNextGeneration(resumed));
}

// The hot sequence lives in RTC, so a deep sleep must not restart numbering.
void test_HotSequenceSurvivesDeepSleep() {
  ProbeRtcState state{};
  ProductProbeColdBootReset(state, 1);
  ProductProbeSetStage(state, ProbeStage::kHotRun);
  for (int i = 0; i < 5; ++i) {
    SendAndConfirm(state);
  }
  // A deep-sleep wake is only a copy of RTC memory into the new boot.
  ProbeRtcState const after_sleep = state;
  TEST_ASSERT_EQUAL_UINT16(5, after_sleep.seq);
  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProbeStage::kHotRun),
                          after_sleep.stage);

  ProbeRtcState resumed = after_sleep;
  TEST_ASSERT_EQUAL_UINT16(6, ProductProbeNextSeq(resumed));
  TEST_ASSERT_EQUAL_UINT16(5, resumed.prev.seq);
}

// A pre-Encode Wi-Fi failure reprobes: stage 0, cleared searches, kept session.
void test_FailureResetsStageAndSearches() {
  ProbeRtcState state{};
  ProductProbeColdBootReset(state, 0xDEADBEEFu);
  ProductProbeSetStage(state, ProbeStage::kHotRun);
  state.profile = 4;
  state.pre_ms = 25;
  state.post_ms = 50;
  ParamSearchRecord(ProductPreTable(), state.pre_search, true);
  PostSearchRecordBatch(state.post_search, FullBatch());
  SendAndConfirm(state);

  ProductProbeFailureResetStage(state);

  TEST_ASSERT_EQUAL_UINT8(static_cast<std::uint8_t>(ProbeStage::kIcmpSelect),
                          state.stage);
  TEST_ASSERT_EQUAL_UINT8(1, state.reprobe_count);
  TEST_ASSERT_EQUAL_UINT32(0xDEADBEEFu, state.session);
  TEST_ASSERT_EQUAL_UINT8(0, state.pre_search.have_best);
  TEST_ASSERT_EQUAL_UINT8(0, state.post_search.have_last_passed);
  TEST_ASSERT_EQUAL_UINT16(300, PostSearchCurrent(state.post_search));
  TEST_ASSERT_EQUAL_UINT8(0, state.pending_flags);
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

// The product RTC state must stay small and free of integrity fields. Pinned so
// the campaign report can quote the RTC cost without recompiling, and so growth
// is a deliberate change. The largest member is 4 bytes wide, so the layout is
// the same on the host and on the 32-bit target.
void test_RtcStateIsCompact() {
  TEST_ASSERT_EQUAL_UINT(108, sizeof(ProbeRtcState));
}

}  // namespace ae::test_product_probe_select

int test_product_probe_select() {
  UNITY_BEGIN();
  RUN_TEST(ae::test_product_probe_select::test_ColdBootStartsAtStageZero);
  RUN_TEST(ae::test_product_probe_select::test_StageAdvanceSaturatesAtDone);
  RUN_TEST(
      ae::test_product_probe_select::test_StageNamesMatchSleepingProbeFlow);
  RUN_TEST(ae::test_product_probe_select::test_OnlySleepingStagesAreMeasured);
  RUN_TEST(
      ae::test_product_probe_select::test_InvalidStageByteReadsAsIcmpSelect);
  RUN_TEST(ae::test_product_probe_select::test_IcmpTrialRequiresAllConnects);
  RUN_TEST(ae::test_product_probe_select::test_IcmpBorderlineTriggersExtension);
  RUN_TEST(ae::test_product_probe_select::test_IcmpSelectionIsReliabilityFirst);
  RUN_TEST(ae::test_product_probe_select::
               test_IcmpSelectionPrefersRicherProfileOnTie);
  RUN_TEST(ae::test_product_probe_select::test_IcmpSelectionReportsNoWinner);
  RUN_TEST(ae::test_product_probe_select::test_PreSearchStartsAtHundred);
  RUN_TEST(ae::test_product_probe_select::test_PreSearchSelectsLowestPassing);
  RUN_TEST(ae::test_product_probe_select::test_PreSearchStopsOnFirstFailure);
  RUN_TEST(ae::test_product_probe_select::test_PreSearchExtendsUpwards);
  RUN_TEST(ae::test_product_probe_select::test_PostTableIsStrictlyDescending);
  RUN_TEST(ae::test_product_probe_select::test_PostFullBatchPassesAndDescends);
  RUN_TEST(ae::test_product_probe_select::test_PostAllPassSelectsZero);
  RUN_TEST(
      ae::test_product_probe_select::test_PostSmallerFailureKeepsLastPassed);
  RUN_TEST(ae::test_product_probe_select::test_PostTotalFailureIsInvalidPath);
  RUN_TEST(ae::test_product_probe_select::test_PostEighteenOfTwentyFails);
  RUN_TEST(ae::test_product_probe_select::test_PostNearMissPassesOnAggregate);
  RUN_TEST(ae::test_product_probe_select::test_PostNearMissFailsOnAggregate);
  RUN_TEST(ae::test_product_probe_select::
               test_PostSecondBatchAllowanceIsPerCandidate);
  RUN_TEST(ae::test_product_probe_select::test_PostUnconfirmedSendBlocksPass);
  RUN_TEST(
      ae::test_product_probe_select::test_PostUnconfirmedSleepRetriesBatch);
  RUN_TEST(ae::test_product_probe_select::test_PostRetryClearsPendingAggregate);
  RUN_TEST(ae::test_product_probe_select::test_QueryOnlyAfterWholeBatchSent);
  RUN_TEST(
      ae::test_product_probe_select::test_PendingSampleNeedsConfirmedSleep);
  RUN_TEST(ae::test_product_probe_select::test_UnconfirmedSleepRejectsSample);
  RUN_TEST(ae::test_product_probe_select::
               test_UnconfirmedTxDoneOccupiesSlotWithoutCounting);
  RUN_TEST(ae::test_product_probe_select::
               test_InvalidatedBatchGetsNewIdAndIsBounded);
  RUN_TEST(ae::test_product_probe_select::test_HotCountersOnlyGrowDuringHotRun);
  RUN_TEST(ae::test_product_probe_select::test_LateQueryAcceptsCompleteCount);
  RUN_TEST(ae::test_product_probe_select::test_LateQueryRetriesWhileCountMoves);
  RUN_TEST(ae::test_product_probe_select::test_LateQueryAcceptsStableCount);
  RUN_TEST(ae::test_product_probe_select::test_LateQueryTimesOut);
  RUN_TEST(
      ae::test_product_probe_select::test_LateQueryBudgetCoversACloudRoundTrip);
  RUN_TEST(
      ae::test_product_probe_select::test_LateQueryAsksAgainWhenUnanswered);
  RUN_TEST(ae::test_product_probe_select::test_PreviousTimingCarriesToNextSend);
  RUN_TEST(
      ae::test_product_probe_select::test_SendGenerationIsStrictlyIncreasing);
  RUN_TEST(ae::test_product_probe_select::test_HotSequenceSurvivesDeepSleep);
  RUN_TEST(ae::test_product_probe_select::test_FailureResetsStageAndSearches);
  RUN_TEST(ae::test_product_probe_select::test_ReprobeCountAccumulates);
  RUN_TEST(ae::test_product_probe_select::test_RtcStateIsCompact);
  return UNITY_END();
}
