# Æther stream matrix benchmark (`aether_stream_matrix_bench`)

Independent console benchmark for **message size × period × stream × transport** over
the live Æther cloud path. One process runs **two registered clients** (`sender`, `receiver`, same parent UID
as other SDK benches) with separate persisted UIDs; payloads traverse the real
network.

## Four modes

| Stream          | Transport | Send policy                         |
|-----------------|-----------|-------------------------------------|
| `P2pStream`     | UDP       | `RequestPolicy::Priority{udp_idx}`  |
| `P2pStream`     | TCP       | `RequestPolicy::Priority{tcp_idx}`  |
| `P2pSafeStream` | UDP       | same UDP server priority on P2P base |
| `P2pSafeStream` | TCP       | same TCP server priority on P2P base |

After connect, the bench checks `stream_info().is_reliable` (UDP → false, TCP → true).
If the desired transport is unavailable: `TRANSPORT_UNAVAILABLE` (no fallback).

## Limits (sources)

### Cloud server (source defaults — `aethernetio/aether`)

From `ClientDescriptorWork.java` (not modified by this bench):

- `msgMaxBytes = 65536`
- `msgQueueLimit = 1000`
- `msgTimeLimit = 60` (seconds)

**Deployed production values:** UNKNOWN (not queried in this run). Planning uses source
defaults. Undelivered messages below 65536 B are reported as measured loss, not as
`SIZE_LIMIT`.

### SDK

- UDP socket MTU budget: **1200 B** application-visible payload cap per datagram path
  (`unix_udp_socket`, `wifi_channel` / `ethernet_channel` UDP `max_packet_size`).
- `AE_SAFE_STREAM_CAPACITY = 20 * 1024` (`aether/config.h`).
- Default bench SafeStream config (same family as `send_message_delays`):
  - `window_size = capacity/2 - 1`
  - `max_packet_size = capacity/2 - 1`
  - `max_repeat_count = 10`
  - `wait_ack_timeout = 1500 ms`
  - `send_repeat_timeout = 200 ms`
- `AE_P2P_MESSAGE_STREAM_BUFFER_CAPACITY = 10` → local backpressure when more than ten
  upper writes are in flight.

Envelope: `BenchEnvelope{sequence, payload}` serialized with `seri::BinaryArchive`
(reflection). Overhead is measured at startup via empty payload.

## Matrix

Payload sweep: 100, 200, 400, … up to mode limit (doubling).

Periods (ms): 1000, 500, 250, 125, 62.5, 31.25, 15.625, 7.8125 — capped by
`--max-rate` (messages/s, default 128).

Each point: warmup (default 5 s) → measure (30 s) → late wait (10 s). Sends use
**absolute schedule** `t0 + n × period` (monotonic clock); no wait for RX between sends.

## Build (Windows x64 Release)

```powershell
cmake -S . -B build-stream-matrix -DCMAKE_BUILD_TYPE=Release -DAE_BUILD_TESTS=OFF
cmake --build build-stream-matrix --config Release --target aether_stream_matrix_bench
```

## Run

```powershell
.\scripts\run_stream_matrix_bench.ps1
```

Or manually from the build directory (after removing stale state if needed):

```powershell
Remove-Item -Recurse -Force .\state -ErrorAction SilentlyContinue
.\aether_stream_matrix_bench.exe --smoke --output stream_matrix_out
.\aether_stream_matrix_bench.exe --full --max-rate 128 --output stream_matrix_out
```

Outputs: `results.csv`, `run.log`, and `summary.md` (from the script).

## CSV `outcome` values

- `OK` — point completed (may include loss/backpressure in counters)
- `SKIP_SIZE_LIMIT` — payload over computed limit for that mode
- `TRANSPORT_UNAVAILABLE` — could not select UDP/TCP for that mode
- `RX_MISSING` — writes succeeded but zero unique receives
