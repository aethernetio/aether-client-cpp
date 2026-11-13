# Load-dependent network saturation

`adaptive-datagram-saturation [runs.csv] [summary.csv]` exercises the existing
`EmulatedDatagramSocket` directly, independently of the adaptive controller.
It fixes the link at 100,000 bytes/s, datagrams at 800 bytes, the serialization
queue at eight datagrams (including the one in service), and propagation delay
at 10 ms. Configured random loss, jitter, duplication, reordering and blackholes
are all disabled. Every drop must therefore be a real finite-queue overflow.

The sweep offers 50, 70, 85, 90, 95, 98, 100, 105, 110, 125 and 150 percent of
capacity, with seeds 12345, 24680, 7, 42 and 999. Each independent run has 10
virtual seconds of warmup and 50 seconds of measurement. It runs two sources:

- Seeded Poisson interarrival times produce ordinary traffic bursts. Even below
  mean capacity these bursts can fill the queue, increasing delay and losses as
  utilization approaches 100 percent.
- Periodic arrivals are a causal control: below or at capacity there is no
  accumulating queue, so delay stays at serialization plus propagation (18 ms)
  and there are no drops. A loss function arbitrarily tied to utilization would
  fail this control.

Capacity is not changed during a run. Loss probability is not assigned from the
offered rate: serialization and the finite FIFO produce the saturation curve.

Measurement selects datagrams by **send time** in the measurement interval.
Warmup traffic still occupies the real queue. After the interval ends, the
network is fully drained and all measured datagrams must be either delivered
or explicitly dropped. Thus packets still in flight cannot masquerade as loss.
Goodput is delivered cohort bytes divided by the 50-second measurement duration;
the final drain is excluded from the denominator. Small finite-cohort boundary
effects are possible. The summary reports weighted loss across five seeds and
the mean of the per-seed nearest-rank latency percentiles, not a pooled p95.
Poisson repeatability is for the same executable/platform (`std::log` is used).

Four Unity tests additionally assert:

1. Aggregate loss and p95 latency rise substantially from low to near-saturated
   and overloaded traffic, while delivered throughput plateaus at capacity.
2. Periodic subcapacity traffic has no invented loss or additional delay.
3. Doubling capacity at the same absolute offered rate reduces loss and delay;
   enlarging the queue instead reduces loss at the cost of higher latency.
4. Reducing load on the same overloaded connection drains the queue and restores
   the 18-ms baseline and loss-free delivery.

These tests validate the network fixture. They do not establish a universal
Internet loss curve or prove that the adaptive controller always finds the
optimal operating point. In particular, finite queues cap the latency of packets
that survive; after that point additional overload mainly increases drops.

Build through the regular root CMake project with tests enabled:

```powershell
cmake --build build-adaptive-debug --config Debug --target test-adaptive-datagram adaptive-datagram-saturation --parallel 4
ctest --test-dir build-adaptive-debug -C Debug -R '^(test-adaptive-datagram|test-network-saturation)$' --output-on-failure
& '.\build-adaptive-debug\tests\run\Debug\adaptive-datagram-saturation.exe' 'saturation-runs.csv' 'saturation-summary.csv'
```
