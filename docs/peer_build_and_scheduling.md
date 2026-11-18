# Peer modules and client send scheduling

Configure modules in the `USER_CONFIG` header, before `aether/config.h` supplies
defaults. These are compile-time definitions, not CMake cache booleans:

| Definition | Scope |
| --- | --- |
| `AE_SUPPORT_P2P` | Entire native authenticated UDP peer engine |
| `AE_SUPPORT_LAN` | LAN discovery, multicast and discovered peer connections |
| `AE_SUPPORT_DIRECT_IP` | Configured IP endpoints and unsolicited direct connections |
| `AE_SUPPORT_HOLE_PUNCH` | Rendezvous client, broker, service, wire helpers and punching setup |

Each child defaults to `AE_SUPPORT_P2P`. That master flag retains its existing
platform/IPv4/UDP/crypto availability checks. A child explicitly enabled with the
master disabled is a configuration error. The existing `AE_SUPPORT_TCP`,
`AE_SUPPORT_UDP`, HTTP and WebSocket flags still control server transports.
The original `P2pStream` cloud messaging API remains available when the *native*
P2P module is excluded.

For cloud-only builds:

```cpp
#define AE_SUPPORT_P2P 0
```

For LAN-only native connections, with normal cloud messaging still available:

```cpp
#define AE_SUPPORT_P2P 1
#define AE_SUPPORT_LAN 1
#define AE_SUPPORT_DIRECT_IP 0
#define AE_SUPPORT_HOLE_PUNCH 0
```

Excluded discovery and rendezvous implementations are removed by preprocessing,
including multicast socket setup and rendezvous classes. Disabled direct IP
entry points reject calls. Disabled route types cannot be selected even when a
runtime stream policy explicitly requests them. Runtime settings that try to
enable an excluded module fail rather than silently enabling another route.
Persisted configuration types remain readable so configuration can be inspected;
loading settings does not bypass the compile-time gates.

## Shared priority queue

Every `Client` owns one runtime `PrioritySendQueue`, shared by its message
streams and destinations. A queued item holds its message before API encoding,
encryption or socket submission. The next eligible item is selected by priority;
equal priorities preserve FIFO order. A route that is not ready can retry without
blocking another queued stream. Runtime queue entries and actions are not saved.

```cpp
StreamDefinition file;
file.id = 101;
file.delivery = StreamDelivery::kReliable;
file.sending = StreamSendPolicy{SendPriority::kBackground, 1000};

StreamDefinition keyboard;
keyboard.id = 105;
keyboard.delivery = StreamDelivery::kReliable;
keyboard.sending = StreamSendPolicy{SendPriority::kInteractive, 128};
```

`sending` is inherited independently of routing and delivery type. A child's
explicit settings replace the inherited sending settings. Named priorities are
Interactive (0), Realtime (64), Normal (128), and Background (255); smaller values
go first. Files use available opportunities after control and realtime traffic.
The queue reserves eight of its 64 entries and a socket-sized byte allowance for
interactive work. Producers must observe write results and `StreamInfo` writable
updates when they reach backpressure.

One downstream write is active at a time. Each admission uses one scheduler task
and waits for the current downstream write; there is no fixed per-packet rate cap.
Input event tasks can therefore run between submissions. Socket backpressure
retains the rest in the SDK. An active TCP frame cannot be interrupted:
its remaining bytes must be written before a keyboard frame, otherwise the
receiver's framing would be corrupted. Small packets bound this remaining work.
Stopping a queued action prevents dispatch; stopping an active action waits for
that already started write to finish. Stream destruction removes its queued
work without cancelling other streams.

Delivery sequence numbers are allocated at handoff to a writable route, after
priority selection. A long-waiting file packet therefore cannot fall outside the
receiver's bounded deduplication window because keyboard packets overtook it.
An unsuccessful UDP enqueue is retried with a fresh number before another
handoff; no bytes were accepted by that failed atomic datagram send.
`EAGAIN`/`EWOULDBLOCK` and buffer pressure retain online presence, and other ready
native paths can be tried. They do not force a cloud fallback or mark a peer
offline merely because its local socket buffer is full.

`max_packet_bytes` limits the application payload admitted by the underlying
`P2pStream`, including the existing reliable/adaptive wrapper's protocol bytes.
Reliable streams fragment larger application chunks according to this carrier's
budget. File producers still feed chunks that fit the existing reliable stream
window; an entire arbitrarily large file is not retained in that window.
The default carrier limit is 1200 bytes. A packet plus a conservative 256-byte
allowance must fit `AE_SOCKET_SEND_BUFFER_BYTES`. Rendezvous signaling uses
interactive priority and a larger bounded carrier limit for candidate plans.

## Kernel buffering

`AE_SOCKET_SEND_BUFFER_BYTES` defaults to 4096 (minimum 2048). Native peer UDP
and desktop Windows/Unix TCP and UDP sockets request that `SO_SNDBUF` size.
Failure to set the option fails socket initialization. Linux may double or clamp
the requested value; an OS socket limit is not a byte-for-byte application queue
budget, nor a bound on buffers in a modem, browser, driver or router.
`AE_CLIENT_SEND_QUEUE_BYTES` defaults to 262144 and bounds queued message bytes,
including the active write; it must exceed the socket budget. These limits and
the feature switches are included in compile-options telemetry.

The queue prioritizes messages above transport encoding. Existing server
transport framing and packet queues retain the order of already encoded writes,
and reliability, acknowledgements and deduplication remain in their existing
stream implementations.

## Document-based regression

The fixture `tests/test-peer/fixtures/file_video_hierarchy.json` preserves the
JSON example from [New doc](https://docs.google.com/document/d/1JQzGROuYsSxQHmYdCHwpfrjod_PJ7QdL166TrAWpHXg/edit),
“And a particular case with file transfer and real-time video streaming”.
The C++ integration test instantiates `root/file` and
`root/video_frames/blocks/progressive_img`, plus an interactive keyboard sibling.
It checks inherited budgets/routes/destinations, independent payloads and
statistics, keyboard completion before the background file, and persistence of
definitions with fresh runtime queues on load.

The original JSON is an architectural example, not an accepted SDK JSON schema.
The test maps its tree and delivery roles onto the existing C++ wrappers. It does
not claim implementation of every illustrative QoS, native TCP/DNS, FEC or offline
storage option from that JSON. Offline storage remains explicitly unsupported.
A second deterministic test holds the current downstream write, queues keyboard
events behind file work, and verifies priority, equal-priority ordering and
one-write admission before releasing the simulated socket.
