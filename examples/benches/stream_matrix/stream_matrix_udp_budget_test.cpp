/*
 * Unit tests for UDP datagram budget audit / COMPLIANT|VIOLATED|UNVERIFIED.
 */

#include <cstdlib>
#include <iostream>

#include "stream_matrix/bench_limits.h"
#include "stream_matrix/bench_udp_budget.h"

using namespace ae::stream_matrix_bench;

static int g_failures = 0;

static void Expect(bool ok, char const* msg) {
  if (!ok) {
    std::cerr << "FAIL: " << msg << '\n';
    ++g_failures;
  }
}

static UdpSocketAuditSnapshot BothDirectionsClean() {
  UdpSocketAuditSnapshot s{};
  s.tx_observed = true;
  s.rx_observed = true;
  s.max_tx_payload_bytes = 900;
  s.max_rx_payload_bytes = 1100;
  return s;
}

static void test_compliant_both_directions() {
  auto v = EvaluateUdpBudget(BothDirectionsClean());
  Expect(v.status == UdpBudgetStatus::kCompliant, "both dirs compliant");
  Expect(v.budget_ok(), "budget_ok only when compliant");
  Expect(ClassifyUdpBudgetOutcome(v).empty(), "no outcome string");
}

static void test_tx_only_unverified() {
  UdpSocketAuditSnapshot s{};
  s.tx_observed = true;
  s.max_tx_payload_bytes = 800;
  auto v = EvaluateUdpBudget(s);
  Expect(v.status == UdpBudgetStatus::kUnverified, "tx-only unverified");
  Expect(!v.budget_ok(), "tx-only not pass");
  Expect(v.tx_compliant, "tx path clean");
  Expect(!v.rx_compliant, "rx not verified");
}

static void test_rx_only_unverified() {
  UdpSocketAuditSnapshot s{};
  s.rx_observed = true;
  s.max_rx_payload_bytes = 900;
  auto v = EvaluateUdpBudget(s);
  Expect(v.status == UdpBudgetStatus::kUnverified, "rx-only unverified");
  Expect(!v.budget_ok(), "rx-only not pass");
}

static void test_measured_rx_oversize_violated() {
  auto s = BothDirectionsClean();
  s.max_rx_payload_bytes = 4411;
  s.rx_violations = 2;
  auto v = EvaluateUdpBudget(s);
  Expect(v.status == UdpBudgetStatus::kViolated, "measured rx oversize");
  Expect(ClassifyUdpBudgetOutcome(v) == "UDP_BUDGET_MEASURED_OVERSIZE",
         "oversize class");
}

static void test_emsgsize_at_2048_violated() {
  UdpSocketAuditSnapshot s{};
  s.rx_observed = true;
  s.recv_error_events = 1;
  s.recv_emsgsize_events = 1;
  s.recv_emsgsize_budget_lower_bound = 1;
  s.recv_buffer_bytes = 2048;
  auto v = EvaluateUdpBudget(s);
  Expect(v.status == UdpBudgetStatus::kViolated, "emsgsize@2048 violated");
}

static void test_emsgsize_at_1200_violated() {
  UdpSocketAuditSnapshot s{};
  s.rx_observed = true;
  s.recv_emsgsize_budget_lower_bound = 1;
  s.recv_buffer_bytes = 1200;
  auto v = EvaluateUdpBudget(s);
  Expect(v.status == UdpBudgetStatus::kViolated, "emsgsize@1200 violated");
}

static void test_emsgsize_at_512_unverified() {
  UdpSocketAuditSnapshot s{};
  s.rx_observed = true;
  s.recv_emsgsize_events = 1;
  s.recv_emsgsize_buffer_only = 1;
  s.recv_buffer_bytes = 512;
  auto v = EvaluateUdpBudget(s);
  Expect(v.status == UdpBudgetStatus::kUnverified, "emsgsize@512 unverified");
  Expect(!v.budget_ok(), "512 emsgsize not compliant");
}

static void test_no_observations_unverified() {
  auto v = EvaluateUdpBudget({});
  Expect(v.status == UdpBudgetStatus::kUnverified, "empty unverified");
  Expect(ClassifyUdpBudgetOutcome(v) == "UDP_BUDGET_UNOBSERVED", "unobserved tag");
}

static void test_other_recv_error_unverified_not_violated() {
  auto s = BothDirectionsClean();
  s.recv_other_errors = 1;
  s.recv_error_events = 1;
  auto v = EvaluateUdpBudget(s);
  Expect(v.status == UdpBudgetStatus::kUnverified, "other recv error unverified");
  Expect(ClassifyUdpBudgetOutcome(v) == "UDP_BUDGET_RX_INCOMPLETE",
         "incomplete rx check");
}

static void test_tx_oversize_violated() {
  auto s = BothDirectionsClean();
  s.tx_violations = 1;
  s.max_tx_payload_bytes = 1500;
  auto v = EvaluateUdpBudget(s);
  Expect(v.status == UdpBudgetStatus::kViolated, "tx oversize violated");
}

static UdpSocketAuditSnapshot EmsgsizeBufferOnlyBothDirsSnapshot() {
  UdpSocketAuditSnapshot s{};
  s.tx_observed = true;
  s.rx_observed = true;
  s.max_tx_payload_bytes = 800;
  s.max_rx_payload_bytes = 400;
  s.recv_buffer_bytes = 512;
  s.recv_emsgsize_events = 1;
  s.recv_emsgsize_buffer_only = 1;
  return s;
}

static void test_emsgsize_buffer_only_after_rx_observed_unverified() {
  auto s = EmsgsizeBufferOnlyBothDirsSnapshot();
  auto v = EvaluateUdpBudget(s);
  Expect(v.status == UdpBudgetStatus::kUnverified,
         "RX then EMSGSIZE: not COMPLIANT");
  Expect(!v.budget_ok(), "RX then EMSGSIZE: budget_ok false");
  Expect(!v.rx_compliant, "RX then EMSGSIZE: rx not compliant");
  Expect(v.tx_compliant, "TX still clean within budget");
}

static void test_emsgsize_buffer_only_before_rx_still_unverified() {
  UdpSocketAuditSnapshot s{};
  s.recv_emsgsize_events = 1;
  s.recv_emsgsize_buffer_only = 1;
  s.recv_buffer_bytes = 512;
  s.rx_observed = true;
  s.max_rx_payload_bytes = 400;
  s.tx_observed = true;
  s.max_tx_payload_bytes = 800;
  auto v = EvaluateUdpBudget(s);
  Expect(v.status == UdpBudgetStatus::kUnverified,
         "EMSGSIZE then RX: not COMPLIANT");
  Expect(!v.budget_ok(), "EMSGSIZE then RX: budget_ok false");
}

static void test_emsgsize_lower_bound_still_violated_with_clean_max() {
  auto s = BothDirectionsClean();
  s.recv_emsgsize_budget_lower_bound = 1;
  s.recv_buffer_bytes = 1200;
  auto v = EvaluateUdpBudget(s);
  Expect(v.status == UdpBudgetStatus::kViolated,
         "lower bound violated despite other clean obs");
}

int main() {
  test_compliant_both_directions();
  test_tx_only_unverified();
  test_rx_only_unverified();
  test_measured_rx_oversize_violated();
  test_emsgsize_at_2048_violated();
  test_emsgsize_at_1200_violated();
  test_emsgsize_at_512_unverified();
  test_no_observations_unverified();
  test_other_recv_error_unverified_not_violated();
  test_tx_oversize_violated();
  test_emsgsize_buffer_only_after_rx_observed_unverified();
  test_emsgsize_buffer_only_before_rx_still_unverified();
  test_emsgsize_lower_bound_still_violated_with_clean_max();
  if (g_failures != 0) {
    std::cerr << "stream_matrix_udp_budget_test: " << g_failures << " failure(s)\n";
    return 1;
  }
  std::cout << "stream_matrix_udp_budget_test: OK\n";
  return 0;
}
