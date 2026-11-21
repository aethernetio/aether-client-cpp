# Shared resource for Raw sends on Windows

Create one runtime `PrioritySendQueue(context, RawResourceConfig{...})` per
explicit shared outbound bottleneck. Pass the same shared pointer to each
participating `P2pStream`, including streams to different UIDs. Supply another
instance for an independent path. Run all calls, event dispatch and teardown on
the same SDK event loop. The context and clients must outlive these resources.
On route changes that invalidate the grouping or RTT baseline, close and bind
the streams to the intended resource. There is no automatic path inference.

```cpp
auto resource = std::make_shared<ae::PrioritySendQueue>(
    context, ae::RawResourceConfig{});
ae::P2pStream outgoing{
    context, sender, peer_uid,
    sender->message_stream_manager().CreatePort(peer_uid, 91),
    ae::RequestPolicy::MainServer{}, route,
    ae::StreamSendPolicy{ae::SendPriority::kInteractive, 1000},
    ae::RawSendBinding{resource, 1}};

auto* write = outgoing.TryWriteRaw(std::move(payload));
```

`TryWriteRaw` returns null and preserves the input on rejection. Respect the
advertised element limit and available credit. Observe `stream_update_event()`
for changes to writability, and own write-status subscriptions until completion.
`Write()` on a bound stream uses the same admission path and produces a failed
action on rejection. A completed local write communicates the lower write
status. Receiver delivery is observed separately. Stop a queued action with
`Stop()`. Stop on an already handed-off write reports stop at completion. Stream
destruction cancels its queued work and detaches pending completion handlers.
A packet handed to transport can still arrive. Drive the SDK scheduler for
delayed pacing, queue expiry, completions and reclamation.

## Endpoint agreement

Dedicate one stream ID to each balanced direction for that pair of UIDs. The
sending end consumes return receipts on that ID. For traffic going the other
way, use a second ID. Configure a regular Raw `P2pStream` on the receiving end
for the reciprocal UID and the same ID. The receiver owns a `RawReceiptSink`
and retains its subscription. In the example, `on_message` consumes a view
synchronously or copies it before the callback returns.

```cpp
ae::RawReceiptSink receipts;
ae::Subscription received = incoming.out_data_event().Subscribe(
    [&](ae::DataBuffer const& frame) {
      if (auto payload = receipts.Receive(incoming, frame)) {
        on_message(*payload);
      }
    });
```

The application-port data prefix is 17 bytes. Version/type byte 0x10 precedes
a 64-bit little-endian random resource epoch and a 64-bit little-endian sequence.
The receipt is the same 17 bytes with type 0x11 and no payload. The stream's
configured packet budget includes the prefix. Leave room for outer Æther and
transport framing. The native peer UDP transport enforces its encrypted
datagram limit. The resource charges payload + 17 + a 128-byte outer allowance.
The allowance is a scheduling estimate. Capture actual framing on the deployment
path when dimensioning a link.

A receiver attempts at most one receipt per incoming data frame and keeps at
most one local feedback write outstanding per sink. Busy or lost feedback is
omitted. The SDK authenticates and routes frames according to the selected transport.
The balancer matches epoch, sequence and local flow, ignores stale or duplicate
receipts, and never retransmits application data. Feedback timeout means the
outcome is unknown. Both endpoints must opt into this application-port
contract. The existing server delivery envelope and persistence format are
unchanged.

## Resource policy

Balanced writes enter the resource queue directly at the existing
`PrioritySendQueue` dispatch boundary and call the normal Raw `SendNow`.
There is one payload queue and at most one selected local writer per resource.
A temporarily blocked pre-handoff dispatch yields to eligible flows. A held
lower write serializes that resource. Timeouts expire queued entries, including
while a lower write is pending. Other resources have separate writer gates.

For each eligible FIFO head the scheduler minimizes accumulated virtual
service plus charged packet size divided by effective weight. Class factors
for interactive, realtime, normal and background are 8, 4, 2 and 1. Multiply
by the per-flow weight (1 through 16). Flows entering from idle start at least
at current virtual time. These are work-conserving relative shares among
continuously eligible, backlogged senders. Under that condition and with enough
time before expiry, each flow makes progress. Admission, physical outages,
timeouts and packet loss also affect delivered shares. Higher priority has a
bounded advantage. An idle class makes its capacity available to active flows.

Defaults reserve two queue slots and 8192 charged bytes per flow with up to 32
flows. The product of configured flow slots and entries per flow may be at most
64. A resource additionally caps unconfirmed transmissions at 1 MiB and 4096
records. It retains timestamps, flow and size after local handoff. A receipt or
timeout releases flight credit. Close preserves outstanding charges until
timeout to prevent quick rebinds from manufacturing credit. Metadata and
queued payload storage are bounded independently. Application-owned buffers,
kernel buffers and downstream network queues are outside these limits.

The feedback loop uses a monotonic clock, sender-measured RTT above each
flow's observed minimum, unique acknowledged bytes per interval and missing
receipts. A paced byte rate plus an RTT-based flight window limits bursts.
The implementation targets 30 ms of *additional RTT* by default. It increases
rate on healthy delivery, drains on elevated delay and backs off on missing
feedback. The configured rate floor enables probing after silence. Queue
lifetime defaults to 200 ms. These parameters are operating targets. The loop
cannot recall a datagram or establish a one-way deadline from RTT.

## Windows reproduction

In PowerShell from the SDK checkout, using MSVC 18, CMake and Python 3:

```powershell
$env:CL = '/MP5 /wd4702'
cmake -S . -B build-raw -G "Visual Studio 18 2026" -A x64 -DAE_ENABLE_STREAM_HIERARCHY=OFF -DAE_SUPPORT_ADAPTIVE_DATAGRAM=OFF -DAE_BUILD_TESTS=ON -DAE_BUILD_TOOLS=OFF -DAE_BUILD_EXAMPLES=OFF -DAE_INSTALL=OFF
cmake --build build-raw --config Debug --target test-peer raw-resource-lab --parallel 3
ctest --test-dir build-raw -C Debug -R '^test-peer$' --output-on-failure
New-Item -ItemType Directory -Force measurements | Out-Null
& (Get-ChildItem build-raw -Recurse -Filter raw-resource-lab.exe).FullName measurements 20261121
python tests/raw-resource-lab/verify.py measurements
```

The warning switch addresses MSVC C4702 in the pinned ae-numeric fixed-point
header during Debug compilation. The lab uses virtual time with finite byte
queues and serialization on both paths, separate propagation and jitter,
receiver-generated reverse receipts, and deterministic loss keyed to offered
packet identity. The plain Raw model uses strict priority for simultaneously
ready writes and an immediately accepting local UDP writer. The shared network
queue drops at capacity. This baseline models the SDK policy and is distinct
from a measured Windows UDP throughput run.

The lab writes per-second resource windows, per-flow allocation and delay,
per-flow counts, and aggregate delay percentiles. Source traffic lasts 40 s
with a 3 s drain. On step cases, phases change at 12 s and 24 s. Read delivered
payload bytes over multi-second intervals for useful goodput. The Windows
peer suite separately exercises bound Raw streams over authenticated localhost
UDP, including a single receipt credit shared across two destination UIDs.
