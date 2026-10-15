# Æther stream matrix benchmark (`aether_stream_matrix_bench`)

Console benchmark: **payload size × period × P2pStream/P2pSafeStream × UDP/TCP** over the live Æther cloud. One process, two clients (`sender` / `receiver`), shared monotonic clock.

## Connection model (fixed)

- Both clients call `CreatePort(peer_uid)` **before** any measurement traffic.
- `P2pStream` is constructed on both sides with those handles (no wait on `new_port_event` for setup).
- SafeStream wraps both P2p legs when selected.
- **Service envelope** (kind=`service`, 16-byte payload) verifies delivery before the matrix; not counted in stats.

## Transport selection (no server priority)

Modes do **not** use `RequestPolicy::Priority`. Transport is determined by the **linked P2p** path:

- Verify `p2p_a` and `p2p_b` `stream_info()` with `VerifyP2pTransport()` (`is_reliable`: UDP=false, TCP=true).
- SafeStream `is_reliable` is **not** used for this check.

Compile-time SDK filter (applies to whole SDK via `USER_CONFIG`):

| Header | Effect |
|--------|--------|
| `examples/benches/stream_matrix/config/udp_only.h` | `AE_SUPPORT_UDP=1`, `AE_SUPPORT_TCP=0` |
| `examples/benches/stream_matrix/config/tcp_only.h` | `AE_SUPPORT_UDP=0`, `AE_SUPPORT_TCP=1` |

Example UDP-only configure:

```powershell
cmake -S . -B build-stream-matrix-udp -DCMAKE_BUILD_TYPE=Release `
  -DUSER_CONFIG=examples/benches/stream_matrix/config/udp_only.h `
  -DAE_BUILD_TESTS=OFF
```

If the linked cloud path does not match the requested mode: `TRANSPORT_UNAVAILABLE` (no fallback).

## Server limits (source defaults — `aethernetio/aether`)

From `ClientDescriptorWork.java` / `AuthorizedApiImpl.java` (read-only reference):

| Parameter | Source default | Applies to |
|-----------|----------------|------------|
| `msgMaxBytes` | 65536 | `Message.data.length` on send |
| `msgQueueLimit` | 1000 | pending messages per sender |
| `msgTimeLimit` | 60 s | queue lifetime (capped at 60 s in API) |

**Deployed production values:** UNKNOWN (not queried). Undelivered messages below source limits are reported as measured loss, not `SIZE_LIMIT`.

## Size rules

- **UDP wire budget:** 1200 bytes **full UDP payload** (Æther framing + crypto + bench envelope + payload). Computed by binary search on serialized envelope size; IP/UDP headers excluded. Fragmentation: **not verified** in this bench.
- **P2pStream/TCP:** no 1200 cap; limited by `min(msgMaxBytes, stream_info.max_element_size)` — not `rec_element_size` as a hard cap.
- **P2pSafeStream:** top-level size from SafeStream config/capacity; may exceed 1200; lower legs must respect server + UDP datagram budget per SDK.

## Matrix schedule

1. Sizes 100, 200, 400, … at **period 1000 ms**
2. Same sizes at 500, 250, 125, … ms (up to `--max-rate`, default 128 msg/s)
3. Optional boundary size at computed limit

Phases per point: **warmup** (idle, default 5 s) → **measure** (default 60 s, absolute send schedule) → **late collect** (default 60 s, no new sends). CLI: `--warmup-sec`, `--measure-sec`, `--late-sec`.

## Build & run

```powershell
.\scripts\run_stream_matrix_bench.ps1          # smoke only
.\scripts\run_stream_matrix_bench.ps1 --full   # smoke then full (separate output dirs)
```

Logic tests (no network):

```powershell
cmake --build build-stream-matrix --config Release --target aether_stream_matrix_logic_test
.\build-stream-matrix\Release\aether_stream_matrix_logic_test.exe
```

## Outcomes

`OK`, `SETUP_FAILED`, `TRANSPORT_UNAVAILABLE`, `SKIP_SIZE_LIMIT`, `RX_MISSING`, `WRITE_BACKPRESSURE`, `NO_MEASURE_TRAFFIC`

Exit codes: `0` at least one OK point; `2` setup but no OK; `3` no mode setup; `1` client acquisition failed.

## Smoke baseline (2026-10-14, this host)

| Mode | Result |
|------|--------|
| TCP P2pStream 100 B @ 1 Hz | OK — 60/60 RX, ~58 ms p50 |
| TCP P2pSafeStream 100 B @ 1 Hz | OK — 60/60 RX |
| UDP P2pStream / P2pSafeStream | TRANSPORT_UNAVAILABLE (cloud path linked as TCP) |

Artifacts: `build-stream-matrix/stream_matrix_out_smoke/results.csv`, `run.log`.
