# Desktop UDP / P2pSafeStream diagnostic iteration

**Client branch:** `cursor/stream-matrix-bench` (merged from `f1cf99f` UDP line)  
**Client HEAD (this iteration):** `9b42cf9256a398ad9bdd80ca235629a2ce2d40f1` (merge UDP line `1fcb25a`)  
**Verified baseline referenced:** `f1cf99f91b8e6aea9988482b49c8f4e69270279f`  
**Build:** `build-stream-matrix-udp/Release`, `AE_ADDRESS_SANITIZE=OFF`  
**ASan producer/lifetime:** NOT_RUN (MSVC Release)

## 1. Windows recv / EMSGSIZE vs budget 1200

| Observation | Interpretation |
|-------------|----------------|
| Recv buffer **2048 B** + `WSAEMSGSIZE` | Datagram **> 2048 B** (strict lower bound), therefore **> 1200 B budget** → `UdpBudgetStatus::VIOLATED`, outcome `UDP_BUDGET_EMSGSIZE_LOWER_BOUND` |
| Recv buffer **512 B** + EMSGSIZE | Proves only **> 512 B**; **not** budget violation alone → `UNVERIFIED` (`recv_emsgsize_buffer_only`) |
| Measured RX payload **4411 B** (64 KiB recv) | `UDP_BUDGET_MEASURED_OVERSIZE`; `delivery_ok` may still be true → **`budget_ok` false**, `valid_goodput` false |

Audit does **not** treat recv buffer size as the lost packet size; trace uses `datagram_min_bytes=recv_buffer+1`.

## 2. SafeStream whole-record vs datagram budget

- `ApplyBudgetFlags()` uses **`EvaluateUdpBudget()`** on socket observations only (no 1200 B cap on whole SafeStream write).
- `ClassifyUdpMeasureOutcome()` applies **`UDP_ENVELOPE_ESTIMATE_EXCEEDED`** only to **`P2pStream`**, not `P2pSafeStream`.
- Fragment sizing: `stream_matrix_udp_serialization_test` — large SafeStream payload splits to datagrams ≤ budget.

## 3. Write OK vs application RX (loopback)

`aether_stream_matrix_p2p_safe_chaos_loopback_test` (4×1024 B THPT, async delivery):

| Scenario | THPT / integrity | kSuccess vs byte ranges |
|----------|------------------|-------------------------|
| No loss | OK | All kSuccess ↔ full ranges |
| Single fragment drop + retry | OK | Match |
| Coalesced oversize drop | OK if recovered | Match |
| Lost ACK | OK | Match |
| Reorder (hold wire seq 3) | OK | Match |
| Duplicate (wire seq 2) | OK | Match |
| Sustained loss after handshake | Fail | **No kSuccess without full range** |

ACK replay at emission time: covered by `stream_matrix_safe_stream_ack_check_test`.

**Conclusion:** No reproduced “Write kSuccess but missing bytes” on B in loopback; EMSGSIZE on cloud path is **recv truncation**, not SafeStream ACK skip.

## 4. Trace (diagnostic mode)

Verbose UDP trace (`BeginUdpDatagramTrace`, `verbose=true`): socket TX/RX with `side`, `endpoint`, `transfer_id`, `datagram_id`, `payload_bytes`; SafeStream send/recv/ack with offsets and `repeat_count`. No per-datagram flush unless verbose. No secrets in trace.

Artifacts (local, not in git): `artifacts/udp_p2p_diagnostic/*.log`, prior cloud captures under `artifacts/stream_udp_four_block_*`.

## 5. Server blocker

See **`docs/server_udp_packetization_issue.md`** (Java @ `defaae3e566dc901c8266c7120c74f4e692702a44f`, `aether-server-ref`). **Deployed backend: UNKNOWN.** Fix: pack whole `LazyPacket` units with encryption size estimate; no ciphertext tail-chop.

## 6. Tests run (Release)

| Target | Exit |
|--------|------|
| `aether_stream_matrix_udp_budget_test` | 0 |
| `aether_stream_matrix_udp_serialization_test` | 0 |
| `aether_stream_matrix_p2p_safe_chaos_loopback_test` | 0 |
| `aether_stream_matrix_safe_stream_ack_check_test` | 0 |

No full cloud matrix / no two-minute sweeps this iteration.
