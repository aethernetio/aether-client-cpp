# Adaptive datagrams over the live Aether transport

`P2pAdaptiveDatagramStream` owns a dedicated `P2pStream`
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
handoff; `delivery_event()` reports the peer outcome. An ACK immediately returns
in-flight credit while outcome metadata remains in bounded history until
maturation. A missing old ACK does not block newer sequence numbers; a packet
that leaves the receipt bitmap without an ACK remains unconfirmed until its
outcome matures. L/W/D are adaptation goals; unknown feedback and local expiry are
reported separately from measured network loss. See the emulation example for
the controller's confidence and outcome maturation rules.

## Build and run

Build with the root CMake project and separate build directories. Registration
uses TCP. Measurement uses a UDP-only SDK so a fallback cannot invalidate it.

```powershell
cmake -S . -B build-live-bootstrap -DAE_FILTRATION=ON -DAE_DISTILLATION=OFF `
  -DAE_BUILD_TESTS=OFF -DAE_BUILD_EXAMPLES=ON `
  -DUSER_CONFIG=examples/benches/presence_api/user_config.h
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

The bootstrap and UDP build must use matching cryptography and persisted-state
settings. The supplied UDP configuration shares the Hydrogen benchmark settings;
it disables TCP and WebSocket transport for measurement.

Positional arguments, in order:

| Argument | Range | Default |
|---|---|---:|
| Output CSV | Writable path | `adaptive-live.csv` |
| Measurement seconds | 1..600 | 10 |
| Allowed loss, per mille | 0..1000 | 50 |
| Loss window, milliseconds | 1..60000 | 1000 |
| Delivery target, milliseconds | 1..5000 | 100 |
| Assigned rate ceiling, bytes/s | 1..1000000000 | 4000000 |
| Initial payload bytes | 32..65535, or 0 for current path budget | 800 |
| Unacknowledged packets in flight | 1..512 | 256 |
| Payload growth ceiling | 0 disables; otherwise initial size..65000 | 0 |
| Local pending queue depth | 1..128 | 1 |

The actual payload must fit the linked path and protocol overhead. Growth
requires an explicit nonzero initial payload. The library configuration also
validates relationships between the requested timing parameters. Allowed loss
is a requirement, not an injected packet-loss rate: 10 per mille means 1%, and
100 per mille means 10%.

For example, compare fixed 9507-byte payloads with local queue depths 1 and 128,
using separate output paths and sequential runs:

```text
<absolute-path>/adaptive-datagram-live queue1.csv 180 100 1000 1000 64000000 9507 256 0 1
<absolute-path>/adaptive-datagram-live queue128.csv 180 100 1000 1000 64000000 9507 256 0 128
```

Exit codes: 0 functional checks passed; 1 peer outcomes/integrity/accounting
failed; 2 invalid arguments/output; 3 registration state or clients unavailable;
4 wrong build; 5 UDP route/budget unavailable or lost. Live network benchmarks
are deliberately not CTest tests.

The benchmark uses two clients in one process, with a shared SDK clock and
distinct pinned session IDs. It fills the selected bounded local queue before
driving the SDK scheduler. This depth counts unsent application messages, not
parallel `WSASend` calls. The adaptive stream still permits one active lower
write; the separate in-flight limit counts packets awaiting peer confirmation.
Three seconds of warmup precede measurement; D + feedback timeout + one second
of draining follows it. All accepted outcomes must finalize, payload integrity
and deduplication must hold, and peer delivery plus feedback must exist.
Local socket success alone cannot produce PASS. A PASS does not establish that
every loss window meets the target or that maximum capacity has been reached.

## Optional payload growth

`PayloadSizeController` is a producer policy used by this example; the stream
does not resize arbitrary application messages. Once per second it examines
the latest ten received-byte-rate samples. A probe requires at least seven
healthy nonzero samples, healthy current feedback, coefficient of variation
no greater than 35%, and a last-five-sample mean at least 80% of the first-five
mean. Unknown feedback prevents success confirmation without erasing all
recent observations.

A probe increases future payloads by 25%, bounded by the requested ceiling and
both endpoints' budgets. After ten observations it retains the size only when
feedback is sufficiently healthy and average received goodput improves by more
than 2%. Otherwise it restores the previous size; a well-observed unsuccessful
probe also caps further growth for that run. Confirmed overload halves the size
to no less than its initial value and resets the observation window. These are
heuristics, not a guarantee of finding the optimal size within a run.

The supplied Windows configuration opts into `AE_ENABLE_LARGE_UDP_DATAGRAMS=1`,
raising Ethernet/Wi-Fi UDP channel capacity to 65507 bytes while preserving the
recommended 1200-byte size. The setting is also applied when building transports
from loaded channel profiles: each transport build reapplies the current build's
UDP policy rather than trusting a saved limit. With the macro set to 0, or on
other platforms, the maximum is restored to 1200 bytes. Large datagrams rely on
network-stack fragmentation/reassembly; there is no application fragmentation.
The real path or relay may impose a smaller effective limit.

## Measurements

The main CSV separates offered application bytes, received useful bytes and the
assigned rate. Assigned rate is not measured throughput. Per-second rows include
payload size/budget, queue and in-flight occupancy, confirmed loss/confidence,
delivery delay quantiles, and outcome counters. Row intervals and elapsed times
are included so delayed sampling can be accounted for.

Additional files retain message-level observations:

- `.offered.csv`: sequence, preparation time, measurement-cohort flag and payload
  bytes for each offered message.
- `.latency.csv`: received sequence, delay, receive time, measurement-cohort flag
  and actual payload bytes. Use actual per-message sizes when payloads vary.
- `.controller.csv`: periodic path-delay/RTT samples, rate and confidence,
  queue depth and in-flight count.
- `.local-write.csv`: sequence, queue wait, lower-write wait, completion epoch
  and local success. The optional `Config::on_local_write_complete` observer
  measures admission to lower `Write()` and that call to its local status
  callback, including synchronous work. It excludes feedback, rejected writes
  and expiry before submission. It is neither exact `WSASend` timing nor peer
  ACK latency. Timing distributions are conditional on reaching local completion.

Application delivery delay runs from the timestamp before `Write()` admission
and payload preparation to the receiver callback, including local queue/pacing.
It is not RTT. The integrity check allows 1 ms beyond D for harness work before
admission and after dispatch; consumers can apply the exact configured deadline
to raw samples. Missing packets have no delay sample. Read expired, late,
unconfirmed and local-failure totals alongside the delivered-packet quantiles.
The 32-byte measurement header is included in reported application payload.

Final goodput is the received bytes of the cohort offered during measurement,
including arrivals during draining, divided by measurement duration. Compute
loss from offered and received sequences, independently of sender-side
unconfirmed outcomes. Final counters include warmup and draining; do not divide
them by the measured cohort size. Network startup delays use a steady clock.

For separate processes or devices, provide a known and maintained peer clock
offset. RTT/2 does not establish a one-way deadline. This example does not
implement a clock synchronization or session negotiation service.
