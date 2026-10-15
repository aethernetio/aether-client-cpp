# Æther stream matrix benchmark (`aether_stream_matrix_bench`)

Console benchmark: **payload size × period × P2pStream/P2pSafeStream × UDP/TCP** over the live Æther cloud. One process, two clients (`sender` / `receiver`), shared monotonic clock.

## Dual SDK builds + TCP bootstrap

Æther **client registration requires TCP**. UDP measurement uses a **UDP-only SDK** (`AE_SUPPORT_TCP=0`) that loads clients from persistent `state/` prepared by a **separate TCP process**.

| Step | Build / tool | Role |
|------|----------------|------|
| 1 | `aether_stream_matrix_bootstrap` (TCP SDK, `AE_FILTRATION=ON`) | Register sender/receiver, save `state/`, verify reload |
| 2 | `build-stream-matrix-tcp` | TCP matrix smoke (own copy of seed state) |
| 3 | `build-stream-matrix-udp` (`udp_only.h`) | UDP matrix smoke (seed state copy; **no in-process registration**) |

Orchestration: `scripts/run_stream_matrix_bench.ps1` (Release tests → bootstrap if needed → TCP smoke → UDP smoke → optional `--full` after 4/4).

Configure with **filtration** so startup does not `CleanUp()` domain storage:

```powershell
cmake -S . -B build-stream-matrix-udp -DCMAKE_BUILD_TYPE=Release `
  "-DUSER_CONFIG:STRING=examples/benches/stream_matrix/config/udp_only.h" `
  -DAE_BUILD_TESTS=OFF -DAE_FILTRATION=ON
```

## UDP-only bench behaviour

- Missing `state/` → exit **4**, log `REGISTRATION_STATE_MISSING` (no UDP registration attempt).
- Transport selection uses SDK channel list with **TCP compiled out**; no bench `Restream` hacks.
- `VerifyP2pStreamTransport(UDP)` on measured P2P traffic.

## Connection model

- Both clients call `CreatePort(peer_uid)` before measurement traffic.
- Symmetric `P2pStream`; optional `P2pSafeStream` wrapper on both sides.
- **Service exchange** before the matrix; each run has a new **`run_id`**.

## Stats, scheduler, UDP budget

- Scheduler: `EvaluateScheduleSlot()` (see `aether_stream_matrix_schedule_test`).
- Bench timing: `steady_clock`; SDK pump uses `ae::Now()` with duration-only waits.
- UDP **envelope estimate** (36+96 B) is not proof of wire size; **socket audit** on UDP TX/RX flags \>1200 B. SafeStream per-datagram audit: **not verified**.

## Smoke vs full matrix

- **Smoke:** 100 B @ 1 Hz per mode; exit **0** only when **four** modes pass (both builds).
- **Full matrix:** only after 4/4 smoke (`--full`).
