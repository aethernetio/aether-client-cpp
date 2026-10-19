# UDP / P2pSafeStream diagnostic report (2026-10-19, rev 3)

Branch: `cursor/stream-matrix-bench`  
Client repo: `C:\Users\nickc\Projects\aether-client-cpp`  
Server sources: `C:\Users\nickc\Projects\aether-server-ref` @ **`defaae3e566dc901c8266c7120c74f4ddf70a44f`** (unchanged this iteration)

## Status (this iteration)

| Item | Result |
|------|--------|
| Client desktop checks (budget + loopback) | **Done** (Release tests below) |
| Server UDP packetization | **Not fixed** (no server/deploy changes) |
| Valid max UDP goodput under COMPLIANT TX/RX ≤1200 | **Not established** (no new speed claim) |
| SafeStream protocol bug reproduced in loopback | **No** |

## False positives fixed

### 1. UDP budget: COMPLIANT after EMSGSIZE @ small recv buffer

**Old behavior:** With both TX and RX observed, clean `max_tx`/`max_rx`, and `recv_emsgsize_buffer_only > 0` (recv buffer &lt;1200, lost datagram size unknown), `EvaluateUdpBudget()` still set `rx_compliant=true` → **COMPLIANT**.

**Counterexample (now UNVERIFIED, `budget_ok=false`):**

- `tx_observed`, `max_tx_payload_bytes=800`
- `rx_observed`, `max_rx_payload_bytes=400`
- `recv_buffer_bytes=512`, `recv_emsgsize_buffer_only=1` (order of RX vs EMSGSIZE in the audit snapshot does not matter)

**Fix:** `rx_compliant` requires `recv_emsgsize_buffer_only == 0`. EMSGSIZE with recv ≥1200 remains **VIOLATED** via `recv_emsgsize_budget_lower_bound`.

Tests: `aether_stream_matrix_udp_budget_test` (Release).

### 2. ACK: end-of-test merge hid gaps

**Old behavior:** `AllAcksCoverRecv()` re-checked every ACK_TX against **final** merged recv ranges → a **late** recv could make an **earlier invalid ACK** look valid.

**Counterexample (strict checker rejects; legacy end-of-test accepts):**

- Session origin 1000; recv only `[1100,1200)`; ACK_TX confirms through 1199 → missing `[1000,1100)`.

**Fix:**

- `SafeStreamAckCoverageValidator` — ring-aware coverage from `coverage_begin`, latched violations, unit-tested (`stream_matrix_safe_stream_ack_check_test`).
- `p2p_safe_stream_chaos_loopback_test` — **event-order replay**: each ACK_TX on leg `B` is checked against recv indices **accumulated so far** in trace order (not the final snapshot). Premature ACK before recv in the timeline still fails.
- Bench trace: ACK_TX side tag uses last recv trace side when role is TX (diagnostic only).

**Note:** Strict origin-gap detection is covered by unit tests; loopback replay uses contiguous-at-time checking aligned with THPT trace indices (not a proof of all cloud paths).

### 3. Sustained loss: `kSuccess` without THPT bytes

**Old behavior:** `!thpt_ok || !ExpectSuccessWrites(r)` allowed incomplete RX with all writes reporting `kSuccess`.

**Fix:** `WritesMatchReceivedBytes()` — each settled `kSuccess` write must have full 1024-byte THPT range received. Sustained-loss scenario requires incomplete THPT and no false `kSuccess`; recovery scenarios still require full THPT + four real `kSuccess` + ACK replay OK.

## Release tests run (local)

| Test | Result |
|------|--------|
| `aether_stream_matrix_udp_budget_test` | OK |
| `aether_stream_matrix_safe_stream_ack_check_test` | OK |
| `aether_stream_matrix_p2p_safe_chaos_loopback_test` | OK |

Build dir: `build-stream-matrix-udp` (Release). Cloud matrix **not** run this iteration.

## Budget audit summary (unchanged semantics)

- **`budget_ok`** only when **COMPLIANT** (both TX and RX observed, both directions clean).
- EMSGSIZE recv ≥1200 → **VIOLATED**; recv &lt;1200 buffer-only EMSGSIZE → **UNVERIFIED**.
- **`valid_goodput_mib_s`** empty unless COMPLIANT.

## Trace correlation (diagnostic modes only)

- Socket audit: `socket_id`, `endpoint`, `local_event_id`.
- SafeStream: `correlation_id`; `ack_tx` / `ack_rx`; ACK_TX side from recv leg when trace order allows.

## Valid max speed under budget (unchanged)

Prior cloud observation ~**0.0084 MiB/s** (out=1, 1024 B, burst, COMPLIANT @2048 recv) — **not** re-run or updated here. Do **not** treat test-harness fixes as a new throughput result.

## Artifacts (local, not in git)

`artifacts/stream_udp_*`, build trees under `build-stream-matrix-udp/`, etc.
