# Server UDP datagram coalescing (>1200 B budget)

Studied Java sources at **`defaae3e566dc901c8266c7120c74f4ddf70a44f`** (local clone:
`C:\Users\nickc\Projects\aether-server-ref`). This document does **not** assert which
Netty/NIO/virtual backend is deployed in production; it describes behavior implied by
the shared FastMeta flush path used on UDP connections in that tree.

## Minimal client reproduction (observed)

- Mode: `--streaming-udp-four-block` (4×1024 B THPT chunks, `outstanding=4`, burst).
- Windows client default UDP RX buffer **2048 B** (`AE_WIN_UDP_RECV_BUFFER_BYTES` in
  `aether/config.h`): run fails with **`WSAEMSGSIZE (10040)`** — datagram never
  delivered to userspace; lower bound **>2048 B** (implies **>1200 B** budget).
- Diagnostic RX buffer **65536 B** (`examples/benches/stream_matrix/config/udp_only.h`
  only): application delivery completes, but socket audit records **`max_rx_payload`**
  up to **4411 B** (four-block) and **13245 B** (full 1 MiB / o=4) with hundreds of
  **`rx_violations`** while **client TX ≤1200 B**.
- Artifacts (not in git): `artifacts/stream_udp_four_block_20261019/`,
  `artifacts/stream_udp_four_block_recv64k_20261019/`,
  `artifacts/stream_udp_mi1_o4_recv64k_20261019/`.

## Outgoing datagram formation chain (source)

1. **`MetaContextBase.remoteDataToArray(DataOut)`** — drains **`toRemote`** queue:
   every pending **`LazyPacket`** is **`serialize()`d** into one contiguous buffer with
   **no size cap**.

   Path: `fast-meta/src/main/java/io/aether/net/fastMeta/MetaContextBase.java` (lines
   ~524–529 @ `defaae3e`).

2. Nested API / framing — child contexts and **`LazyPacket`** serialization append
   protocol units back-to-back in flush order (same method; tree walk in **`size()`** /
   child publications).

3. Encryption — e.g. cloud P2P **`sendEncryptedPayload`** encrypts the flushed byte
   array as a whole before wire send.

   Path: `server-cloud/.../MyWorkServerP2PRootApi.java` (`onFlushData` →
   `sendEncryptedPayload` → `P2PStream.Out.send(encrypted)` @ `defaae3e`).

4. **`onFlushData` / flush callback** — user thread triggers network write with the
   combined payload.

5. **UDP send** — Netty UDP connection handler builds **one `DatagramPacket`** per
   flush carrying the **entire** `remoteDataToArray` output:

   Path: `net-netty/.../NettyServerConnectionHandler.java` **`handleFlush`** (~216–250):
   `context.remoteDataToArray(combinedData)` → `payload = combinedData.toArray()` →
   single **`DatagramPacket`**.

   Virtual UDP transport: `net-virtual/.../UdpTransport.java` **`send(byte[])`** — one
   datagram per call, no splitting.

## Where datagram boundaries are lost

- **Queue drain semantics**: `remoteDataToArray` loops `toRemote.poll()` until empty,
  concatenating **all** queued protocol units into **one** byte array per flush.
- **Flush trigger**: multiple nested **`onFlushData`** / auto-flush paths can enqueue
  several messages (e.g. multiple stream/API writes) before the transport flush runs.
- **Post-encryption**: ciphertext is treated as an opaque blob; there is **no** step
  that re-splits on **1200 B** (or MTU) before `DatagramPacket` creation.

## Confirmed vs inferred

| Claim | Evidence |
|-------|----------|
| Client sees RX UDP payloads **>1200 B** when recv buffer allows | Stream-matrix socket audit (`udp_max_rx`, `rx_violations`) on cloud repro @65536 recv |
| EMSGSIZE @2048 recv with no app delivery | Windows audit + `WSAEMSGSIZE` in trace |
| Server **can** emit one UDP datagram = full `remoteDataToArray()` | **Source** @ `defaae3e` (`MetaContextBase`, `NettyServerConnectionHandler`) |
| Production uses this exact Netty UDP handler | **Not confirmed** — only code review at pinned SHA |
| Client SafeStream record size causes oversize UDP | **Ruled out** for server→client path (oversize on **RX** only) |

## Minimal fix plan (server — not applied here)

**Principle:** On each flush, **dequeue whole protocol units** (complete `LazyPacket`s /
atomic API messages) into a batch until adding the next unit would exceed a **post-framing,
post-encryption size budget** (≤1200 B application payload budget on the wire). Emit **one
self-contained datagram**, then continue with the remaining queue.

1. Introduce a **UDP packetization helper** on the flush path (Netty UDP handler and any
   other UDP backend sharing `remoteDataToArray`):
   - Peek/poll `toRemote` **one packet at a time** (or use sized `remoteDataToArray` with
     max bytes respecting **future** encrypt overhead).
   - Account for **encryption expansion** (sym engine overhead) before calling `send`.
   - Loop until queue empty, sending **multiple datagrams** per logical flush if needed.

2. **Do not** truncate tail bytes, split ciphertext arbitrarily, or rely on IP fragmentation.

3. **Do not** change TCP coalescing behavior for UDP’s sake.

4. If a **single atomic protocol unit** exceeds the budget after framing + encryption,
   document required **size contract** or existing fragmentation mechanism — **do not**
   invent a new wire protocol in the client bench.

## Verification criteria (fixed server)

- Four-block cloud repro: **`udp_max_rx ≤ 1200`**, **`rx_violations = 0`**, with default
  **2048 B** recv buffer (no EMSGSIZE).
- Full 1 MiB / o=4: same budget compliance; valid **goodput** only when **`budget_ok`**.
- Unit/integration on server: flushing N small `LazyPacket`s produces **N** datagrams or
  **minimal** coalescing bounded by 1200 B **on the wire after encrypt**.

## Client-side note

Increasing **`AE_WIN_UDP_RECV_BUFFER_BYTES`** remains a **diagnostic** knob only; it is
not a substitute for server packetization and must not be used to claim max speed under
budget.
