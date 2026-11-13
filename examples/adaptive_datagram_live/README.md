# Adaptive datagrams over the live Aether transport

`P2pAdaptiveDatagramStream` owns the corrected streaming branch's `P2pStream`
and runs `AdaptiveDatagramStream` directly over its message boundary. It does
not add SafeStream, retransmissions, ordering, fragmentation, or server changes.
Both clients must use this protocol on a dedicated peer receive port. There is
currently one receive port per peer UID, so do not simultaneously attach an
ordinary P2pStream or P2pSafeStream to the same peer port.

The carrier checks the selected endpoint protocols on every write and receive.
A linked TCP, mixed UDP/TCP, or unidentified endpoint becomes a link error;
neither user datagrams nor protocol feedback can silently use TCP. Application
payload limits include both the P2P transport envelope and the adaptive protocol
allowance. `stream_info()` supplies the current limit and backpressure.

```cpp
ae::adaptive_datagram::Config policy;
policy.max_loss_per_mille = 50;         // L = 5%
policy.loss_window = std::chrono::seconds{1};  // W
policy.latency_target = std::chrono::milliseconds{100};  // D
policy.session_id = local_session;     // nonzero, unique for this instance
policy.peer_session_id = peer_session; // pin the negotiated peer session
policy.peer_clock_offset_us = known_offset;
ae::P2pAdaptiveDatagramStream stream{
    context, client, peer_uid,
    client->message_stream_manager().CreatePort(peer_uid), policy};
// Keep the stream, context/client, and event subscriptions alive.
// Subscribe to out_data_event() and delivery_event(), and pump the SDK normally.
```

All callbacks run on the application thread. Write completion reports local
handoff; `delivery_event()` reports the
peer outcome. L/W/D are adaptation goals; unknown feedback and local expiry are
reported separately from measured network loss. See the emulation example for
the controller's confidence and outcome maturation rules.

## Build and run

Build with the root CMake project and separate build directories. Registration
uses TCP. Measurement uses a UDP-only SDK so a fallback cannot invalidate it.

```powershell
cmake -S . -B build-live-bootstrap -DAE_FILTRATION=ON -DAE_DISTILLATION=OFF `
  -DAE_BUILD_TESTS=OFF -DAE_BUILD_EXAMPLES=ON
cmake --build build-live-bootstrap --config Release --target aether_stream_matrix_bootstrap

cmake -S . -B build-live-udp -DAE_FILTRATION=ON -DAE_DISTILLATION=OFF `
  -DAE_BUILD_TESTS=OFF -DAE_BUILD_EXAMPLES=ON `
  -DUSER_CONFIG=examples/adaptive_datagram_live/config/udp_only.h
cmake --build build-live-udp --config Release --target adaptive-datagram-live
```

Run the bootstrap in a fresh working directory, then run the UDP executable
from that same directory after at least six seconds. The bootstrap writes
`state/` and `stream_matrix_bootstrap.ok`; the live benchmark requires both.
Use absolute executable paths, adjusting the configuration subdirectory for
your generator. Do not run another process with the same client state at once.

```text
<absolute-path>/aether_stream_matrix_bootstrap
<absolute-path>/adaptive-datagram-live live.csv 30 50 1000 100
```

Arguments: output CSV, measurement seconds (1..120), loss target per mille
(0..1000), loss window milliseconds (1..60000), latency target milliseconds
(1..5000). Defaults are `adaptive-live.csv 10 50 1000 100`. Exit codes: 0 checks
passed; 1 peer outcomes/integrity/accounting failed; 2 invalid arguments/output;
3 registration state or clients unavailable; 4 wrong build; 5 UDP route/budget
unavailable or lost. Live network benchmarks are deliberately not CTest tests.

The benchmark uses two clients in one process, with an exactly shared SDK clock
and distinct pinned session IDs. It offers 800-byte payloads continuously,
holding at most one unsent application datagram for the pacer. Three seconds of
warmup precede measurement; D + feedback timeout + one second of draining
follows it. All accepted outcomes must finalize, payload integrity and
deduplication must hold, and end-to-end delivery plus peer feedback must exist.
Only local socket success cannot produce PASS. Delivered application timestamps
allow 1 ms beyond D for harness work before Write admission and after receive
dispatch. A PASS is a functional check, not proof that every network loss window
meets L or that the maximum possible capacity was reached.

CSV rows contain offered and useful bytes per second, adaptive allowance,
confirmed L/W loss and confidence, p50/p95/p99 application delay, p95 absolute
delay variation between consecutive received packets, and its 1/16 EWMA.
The EWMA continues across rows; row percentiles reset each second. Missing
packets have no latency sample; expired/late/unconfirmed/local-failure totals
must be read alongside delay percentiles. Final goodput counts the cohort
offered during measurement, including its arrivals during draining, divided by
measurement duration. Network startup delays are bounded using a steady clock.

For separate processes or devices, provide a known and maintained peer clock
offset. RTT/2 does not establish a one-way deadline. This example does not
implement a clock synchronization or session negotiation service.

Integrated into main with the SDK's current transport and dependency pins.
