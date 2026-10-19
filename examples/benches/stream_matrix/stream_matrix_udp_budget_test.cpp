/*
 * Copyright 2026 Aethernet Inc.
 * Unit tests for UDP datagram budget audit / outcome classification.
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

static UdpSocketAuditSnapshot ObservedClean() {
  UdpSocketAuditSnapshot s{};
  s.socket_observed = true;
  s.max_tx_payload_bytes = 900;
  s.max_rx_payload_bytes = 1100;
  return s;
}

static void test_all_datagrams_within_budget() {
  auto s = ObservedClean();
  auto v = EvaluateUdpBudget(s);
  Expect(v.budget_observed, "socket observed");
  Expect(v.budget_ok, "within budget");
  Expect(ClassifyUdpBudgetOutcome(v).empty(), "no budget outcome");
}

static void test_measured_rx_oversize() {
  UdpSocketAuditSnapshot s = ObservedClean();
  s.max_rx_payload_bytes = 4411;
  s.rx_violations = 3;
  auto v = EvaluateUdpBudget(s);
  Expect(!v.budget_ok, "oversize rx fails budget");
  Expect(ClassifyUdpBudgetOutcome(v) == "UDP_BUDGET_MEASURED_OVERSIZE",
         "measured oversize class");
}

static void test_emsgsize_implies_budget_when_buffer_ge_budget() {
  UdpSocketAuditSnapshot s{};
  s.recv_error_events = 2;
  s.recv_emsgsize_events = 2;
  s.recv_emsgsize_budget_lower_bound = 2;
  s.recv_buffer_bytes = 2048;
  s.socket_observed = false;
  auto v = EvaluateUdpBudget(s);
  Expect(v.budget_observed, "emsgsize counts as observation");
  Expect(!v.budget_ok, "emsgsize@2048 fails budget");
  Expect(v.emsgsize_budget_lower_bound == 2, "lower bound counter");
  Expect(ClassifyUdpBudgetOutcome(v) == "UDP_BUDGET_EMSGSIZE_LOWER_BOUND",
         "emsgsize class");
}

static void test_emsgsize_buffer_smaller_than_budget_not_budget_proof() {
  UdpSocketAuditSnapshot s{};
  s.recv_error_events = 1;
  s.recv_emsgsize_events = 1;
  s.recv_emsgsize_buffer_only = 1;
  s.recv_buffer_bytes = 512;
  auto v = EvaluateUdpBudget(s);
  Expect(v.budget_observed, "recv error observed");
  Expect(v.budget_ok, "512 emsgsize does not prove >1200");
  Expect(v.emsgsize_budget_lower_bound == 0, "no budget lb event");
}

static void test_unobserved_socket_fails_budget() {
  UdpSocketAuditSnapshot s{};
  auto v = EvaluateUdpBudget(s);
  Expect(!v.budget_observed, "unobserved");
  Expect(!v.budget_ok, "budget not ok");
  Expect(ClassifyUdpBudgetOutcome(v) == "UDP_BUDGET_UNOBSERVED", "unobserved class");
}

static void test_other_recv_errors() {
  UdpSocketAuditSnapshot s = ObservedClean();
  s.recv_error_events = 1;
  s.recv_other_errors = 1;
  auto v = EvaluateUdpBudget(s);
  Expect(!v.budget_ok, "other recv error fails budget");
  Expect(ClassifyUdpBudgetOutcome(v) == "UDP_BUDGET_RECV_ERRORS", "recv errors class");
}

int main() {
  test_all_datagrams_within_budget();
  test_measured_rx_oversize();
  test_emsgsize_implies_budget_when_buffer_ge_budget();
  test_emsgsize_buffer_smaller_than_budget_not_budget_proof();
  test_unobserved_socket_fails_budget();
  test_other_recv_errors();
  if (g_failures != 0) {
    std::cerr << "stream_matrix_udp_budget_test: " << g_failures << " failure(s)\n";
    return 1;
  }
  std::cout << "stream_matrix_udp_budget_test: OK\n";
  return 0;
}
