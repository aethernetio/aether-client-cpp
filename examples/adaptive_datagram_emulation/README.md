# Adaptive datagram emulation

This standalone example runs the production `AdaptiveDatagramStream` through
`UdpTransport<EmulatedDatagramSocket, 16>`. It advances 40 seconds of seeded
virtual network time in 1 ms steps, without sleeping or opening real sockets.
It does not measure real-network performance or prove an optimal sending rate.

```text
adaptive-datagram-emulation [csv-path] [seed]
adaptive-datagram-emulation adaptive_datagram.csv 12345
```

Build the `adaptive-datagram-emulation` target using the regular root CMake
project. The default output is `adaptive_datagram.csv` in the working directory;
an existing output file is replaced. The default seed is `12345`. Repeating the
same executable and seed reproduces the CSV. Invalid seeds, output failures,
and scheduler overflow produce a nonzero exit status.

| Virtual time | Forward path |
| --- | --- |
| 0–8 s | 120,000 B/s, 10 ms propagation delay |
| 8–16 s | 25,000 B/s, 4,000-byte link queue |
| 16–24 s | 100,000 B/s, 15 ms jitter, 2% random loss, 3% duplication, 5% reordering with 20 ms extra delay |
| 24–28 s | 100,000 B/s, first 2 datagrams dropped out of every 20 |
| 28–31 s | Healthy forward path at 100,000 B/s; reverse feedback blackholed |
| 31–40 s | Recovery at 120,000 B/s, 10 ms propagation delay |

Other forward impairments reset at each phase. The reverse path is otherwise
120,000 B/s with 10 ms propagation delay. The producer offers 800-byte payloads
at approximately 80% of the current sender allowance and respects backpressure.
It does not deliberately flood the local sender queue.

Each CSV row covers the preceding virtual second. `wire_rate_Bps` is the
controller's current allowance, not measured link throughput. `goodput_Bps` is
the change in useful bytes received by the application during that second.
Loss, latency, stale feedback, local queue and in-flight counts expose control
behavior. `expired`, `lost`, `unconfirmed`, `late`, and `network_drops` are
cumulative; network drops include both directions. An outcome without sufficient
receiver feedback cannot distinguish data loss from ACK loss: it becomes
`unconfirmed` at maturation and stays outside the controller's loss window.
Confirmed missing data requires a receiver report created after its deadline.
An unconfirmed outcome marks feedback incomplete for `W`, so a partially known
window cannot authorize either normal or sparse rate probing.
The simulation stops at 40 s without a drain
period, so its final summary distinguishes finalized unconfirmed outcomes from
still-pending outcomes. A displayed
zero loss with too few outcomes is not a guarantee that the target was met.

The default goals are `L = 50` per mille, `W = 1 s`, and `D = 100 ms`:

- The controller's `L` applies to original datagrams actually submitted to the
  transport that fail to arrive before `D`, including late arrivals. Application
  queue expiry, cancellation, and local write failure have separate stream
  diagnostics and do not count as network loss. `L` is an adaptation goal, not
  a guaranteed maximum loss for every possible network window.
- Outcomes mature uniformly at enqueue time plus `D + feedback_timeout`
  (600 ms with the defaults). `W` counts finalized outcomes by completion time.
  Equal maturation prevents early successful ACKs from biasing the window
  before losses become known; it also delays the loss-based control signal.
  Live clock-corrected path delay (receive time minus wire-send time) provides
  faster protection. Time spent waiting in the application/pacer queue is not
  treated as path congestion. The stream retains enqueue-to-receive timing for
  application deadline results and expires data predicted to miss that deadline
  locally rather than deliberately sending it late.
- This version has no FEC, retransmission, or fragmentation. Each write is one
  independent unordered datagram, bounded by the lower stream's payload limit.
  History, local queue, and in-flight tracking have explicit resource bounds;
  truncated history prevents optimistic rate increases. A full-confidence
  healthy window permits normal probes. At very low rates, too few outcomes
  can fit in `W`; each genuinely new loss-free final delivery can authorize a
  bounded 20% exploration step, at most once per control interval. The window
  remains low-confidence; repeated ACKs or an incomplete/truncated window cannot
  authorize growth. With enough outcomes normal probes remain 5% per two healthy
  control intervals.

Live latency samples are processed once per newly acknowledged source packet.
Repeated reports still establish peer liveness but do not reapply congestion
signals; RTT excludes the receiver's intentional report hold time. Losses from
packets sent before the last backoff remain visible in `L/W` but do not back off
the corrected rate again. A smoothed delay increase over the recent median
baseline detects queue growth before the deadline. On congestion, a fresh
unique-wire-byte delivery estimate bounds the draining rate; it never caps an
otherwise healthy application-limited flow. Carrier changes reset these estimates.

For regression comparisons build `adaptive-datagram-recovery` with tests enabled:

```text
adaptive-datagram-recovery recovery.csv
adaptive-datagram-recovery recovery.csv 12345 --check
```

The first command runs eight independent scenarios for three fixed seeds.
`--check` also enforces minimum useful throughput in the last five seconds on
the predefined healthy/recovery/jitter scenarios. Every mode checks actual
application deadlines, bounded queues and scheduler capacity. CSVs expose
transition losses and unknown outcomes; these tests do not prove Internet
fairness, optimal capacity, or a loss guarantee for an arbitrary network.

Both peers and the task scheduler use the same injected virtual clock here,
with an explicitly known peer offset of zero. Real peers require a known clock
offset supplied through `peer_clock_offset_us`; RTT divided by two does not
establish one-way timing. The injected clock and scheduler must share a timebase.
When connecting through Æther, give each adaptive session a dedicated
datagram-preserving `P2pStream`. This example does not implement a general
multiplexer for unrelated sessions or protocol traffic.

`session_id` identifies the local instance. `peer_session_id = 0` learns the
first peer; a known nonzero ID pins the expected peer and rejects older delayed
sessions. Unlinking and relinking preserves session/deduplication identity. Create
a new stream object to begin a new session rather than reusing sequence identity.
