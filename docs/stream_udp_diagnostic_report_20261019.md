# UDP / P2pSafeStream diagnostic iteration (2026-10-19)

Branch: `cursor/stream-matrix-bench`  
Client repo: `C:\Users\nickc\Projects\aether-client-cpp`  
Prior verified HEAD: `f1cf99f` — this iteration adds budget audit fixes, loopback chaos
tests, verbose trace hooks, and server analysis doc.

## Answers

### Windows `recv` boundary loss (EMSGSIZE)

**Confirmed** when `AE_WIN_UDP_RECV_BUFFER_BYTES=2048`: oversize datagrams are dropped by
the stack (`WSAEMSGSIZE` / 10040) before client parsing. With recv ≥ datagram size,
application-layer delivery can succeed while audit shows **RX >1200 B**.

### ACK semantics under loss

Loopback chaos tests (`p2p_safe_stream_chaos_loopback_test`) with trace hooks show ACK
offsets recorded on **`SendAck`** / inbound **`Ack`** handling; contiguous ACK tracking
does not advance across unsent gaps. Temporary single-packet drop + SafeStream repeat
recovers **full 4096 B** THPT RX without **write OK** vs missing bytes mismatch in
tested scenarios. No client ACK skip bug reproduced in those scenarios.

### `write_ok` vs application RX

Cloud repro: **`delivery_ok`** could be true with **`budget_ok` false** when recv buffer
was widened to 65536 — oversize server datagrams delivered to app. **`ApplyBudgetFlags`**
now clears **goodput** and sets **`OK_BUDGET_EXCEEDED`** or specific **`UDP_BUDGET_*`**
outcomes; **`budget_ok` requires observed socket audit**.

### Client fixes this iteration

- **`EvaluateUdpBudget` / `ClassifyUdpBudgetOutcome`**: separate measured oversize,
  EMSGSIZE lower-bound (recv buffer ≥1200), buffer-only EMSGSIZE, other recv errors,
  unobserved socket.
- Removed SafeStream **record** vs 1200 check from **`ApplyBudgetFlags`** (UDP datagram
  audit only).
- Verbose **`udp_datagram_trace`** only for `--streaming-udp-diagnose` /
  `--streaming-udp-four-block`**; no per-datagram flush on normal matrix runs.
- SafeStream **send/recv/ack** bench trace hooks for causal CSV (`stream_matrix_bench_trace`).
- Tests: `stream_matrix_udp_budget_test`, `p2p_safe_stream_chaos_loopback_test`.

### Server blocker

See **`docs/server_udp_packetization_issue.md`** — **`remoteDataToArray`** drains all
`LazyPacket`s into one buffer; Netty UDP **`handleFlush`** sends **one datagram** per
flush. SHA **`defaae3e566dc901c8266c7120c74f4ddf70a44f`**.

## Artifacts (local, not in git)

| Path | Role |
|------|------|
| `artifacts/stream_udp_four_block_20261019/` | EMSGSIZE @2048 |
| `artifacts/stream_udp_four_block_recv64k_20261019/` | RX>1200 audit |
| `artifacts/stream_udp_mi1_o4_recv64k_20261019/` | Full MiB o=4 diagnostic |
| `C:\Users\nickc\Projects\aether-server-ref` @ `defaae3e` | Server source review |

## Valid max UDP speed (unchanged)

**out=1, 1024 B, burst, budget compliant:** ~**0.0084 MiB/s** (1 MiB in ~117–132 s),
`write_status_trusted=1`, TX/RX audit ≤1200 @2048 recv.
