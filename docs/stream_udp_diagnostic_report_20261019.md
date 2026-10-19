# UDP / P2pSafeStream diagnostic report (2026-10-19, rev 2)

Branch: `cursor/stream-matrix-bench`  
Client repo: `C:\Users\nickc\Projects\aether-client-cpp`  
Server sources: `C:\Users\nickc\Projects\aether-server-ref` @ **`defaae3e566dc901c8266c7120c74f4ddf70a44f`**

## Budget audit (client, local tests)

`EvaluateUdpBudget` returns **`COMPLIANT` | `VIOLATED` | `UNVERIFIED`**:

- **`budget_ok`** only when **COMPLIANT** (both TX and RX observed, both directions clean).
- TX-only or RX-only observation → **UNVERIFIED** (not a pass).
- EMSGSIZE with recv ≥1200 → **VIOLATED** (lower bound > buffer ≥ budget).
- EMSGSIZE with recv <1200 → **UNVERIFIED** (size not proven >1200).
- Other recv errors → **UNVERIFIED** (`UDP_BUDGET_RX_INCOMPLETE`), not measured oversize.
- **`goodput_mib_s`** keeps measured delivery speed; **`valid_goodput_mib_s`** CSV column empty unless COMPLIANT.

Tests: `stream_matrix_udp_budget_test` (Release).

## ACK / SafeStream (local loopback test)

`p2p_safe_stream_chaos_loopback_test` (Release):

- Tracks recv index ranges from SafeStream API recv trace.
- Checks each **ACK_TX** against contiguous recv coverage (no gap skipping).
- Scenarios: no loss, single fragment drop, coalesced datagram drop, ACK drop, permanent loss (no false full success).
- **Does not** prove cloud behavior; **does not** replace server fix.

No client SafeStream bug reproduced requiring protocol changes in this iteration.

## Trace correlation (diagnostic modes only)

- Socket audit: **`socket_id`**, **`endpoint`**, **`local_event_id`** (not a content hash).
- SafeStream: **`correlation_id`** on send; **`ack_tx` / `ack_rx`** roles.
- Side tag from **delivering transport leg** (`SetStreamMatrixTraceSide` on loopback deliver); socket fields from **UdpBase** instance.

## Cloud vs source vs deployed

| Item | Status |
|------|--------|
| Oversized RX / EMSGSIZE @2048 | **Cloud observation** (prior artifacts) |
| `remoteDataToArray` full-queue drain | **Source chain** (Virtual + MetaContext default) |
| `server-core` → `net-virtual` | **Build-time config** |
| Live JVM uses Virtual vs Netty | **UNKNOWN (deployed)** |
| Netty `handleFlush` wired on UDP @ defaae3e | **Not proven** (dead / incomplete in source) |

See **`docs/server_udp_packetization_issue.md`** for file-level call graph.

## Valid max speed under budget (unchanged)

**out=1, 1024 B, burst, COMPLIANT audit @2048 recv:** ~**0.0084 MiB/s** (prior cloud run).  
Recv 65536 and RX>1200 remain **diagnostic only**.

## Artifacts (local, not in git)

`artifacts/stream_udp_four_block_*`, `stream_udp_mi1_o4_recv64k_20261019/`
