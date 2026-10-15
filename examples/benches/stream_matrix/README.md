# Æther stream matrix benchmark (`aether_stream_matrix_bench`)

Console benchmark: **payload size × period × P2pStream/P2pSafeStream × UDP/TCP** over the live Æther cloud. One process, two clients (`sender` / `receiver`), shared monotonic clock.

## Dual SDK builds (required for smoke)

Smoke needs **four modes** from **two** Release builds (do not reuse one build tree for both transports):

| Build dir | `USER_CONFIG` | Modes in-process |
|-----------|---------------|------------------|
| `build-stream-matrix-udp` | `examples/benches/stream_matrix/config/udp_only.h` | UDP P2pStream + P2pSafeStream (matrix) |
| `build-stream-matrix-tcp` | `examples/benches/stream_matrix/config/tcp_only.h` | TCP P2pStream + P2pSafeStream |

**UDP build:** `udp_only.h` enables **TCP for registration/bootstrap** (`FilterProtocol`) but matrix modes are **UDP-only**. `SetupMode` **Restream**s until `VerifyP2pStreamTransport(UDP)` passes — measured sends must not use TCP. Script still runs TCP smoke before UDP smoke (shared `state/` when persistence is available).

Pass config as a **string** (CMake `CACHE PATH` strips `.h` on Windows):

```powershell
cmake -S . -B build-stream-matrix-udp -DCMAKE_BUILD_TYPE=Release `
  "-DUSER_CONFIG:STRING=examples/benches/stream_matrix/config/udp_only.h" `
  -DAE_BUILD_TESTS=OFF
```

Orchestration: `scripts/run_stream_matrix_bench.ps1` (logic tests → **TCP smoke** → **UDP smoke** with shared `state/` → optional `--full` matrix on both exes).

## Connection model

- Both clients call `CreatePort(peer_uid)` before measurement traffic.
- Symmetric `P2pStream`; optional `P2pSafeStream` wrapper on both sides.
- **Service exchange** (bidirectional, unique `run_id` + attempt `sequence`) before the matrix; not counted in stats.
- Each run gets a new **`run_id`**; measure points use `point_id` + `sequence` validation.

## Transport verification

- `VerifyP2pStreamTransport()` checks aggregated send `stream_info`, priority-0 send path, and linked channel endpoint protocol(s) when exposed (no mixed UDP/TCP on linked legs).
- Disabled-at-compile transport → `TRANSPORT_UNAVAILABLE` (not a network fault).

## Stats (per point)

- Warmup: **traffic** (`kWarmup` envelopes), excluded from measure counters.
- Measure window: unique in-window RX, goodput, send/receive rates.
- Late collect: unique late RX (once per sequence), duplicates, latency sample once per sequence; no new measure sends.
- After collect: `missing_after_deadline` among scheduled measure sends.
- Latencies: write→receive, scheduled→write, scheduled→receive.

## UDP 1200-byte socket payload budget

- Budget applies to **UDP datagram payload** after Æther framing + encryption + `SendMessage` body (IP/UDP headers excluded).
- **Envelope estimate** in `bench_udp_budget.cpp` (36 + 96 B + envelope) does **not** prove on-wire size.
- **Socket audit** (`stream_matrix_udp_audit` on UDP TX/RX in `udp.cpp`) counts datagram payloads; \>1200 B → `UDP_SOCKET_BUDGET_VIOLATION`. **SafeStream per-datagram audit: not verified** (whole-message path only).
- TCP and top-level SafeStream message size are not capped at 1200 B.

## Smoke vs full matrix

- **Smoke:** 100 B @ 1 msg/s per mode; all scheduled sends OK; all unique by collect deadline; transport verified; exit **0** only if **both** builds pass their two modes. Partial → exit **2**.
- **Full matrix** (`--full`): only after smoke **0**; loss allowed as measurement; sizes 100, 200, …; periods 1000 ms then halving down to `--max-rate`.

## Artifacts

Per build under `build-stream-matrix-{udp,tcp}/artifacts/smoke_<timestamp>/` and `full_<timestamp>/`: `results.csv`, `run.log`, `build_meta.txt` (SHA, flavor, exe path).

## Logic tests (Release / NDEBUG)

```powershell
cmake --build build-stream-matrix-udp --config Release --target aether_stream_matrix_logic_test
.\build-stream-matrix-udp\Release\aether_stream_matrix_logic_test.exe
```

Non-zero exit on failure (no `assert` dependency).
