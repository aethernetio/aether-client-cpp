# WebSocket transport

The transport uses the ordinary Aether registration, crypto, client, Presence
and P2pStream APIs. RFC 6455 binary messages carry the same PackedSize-framed
packets as the TCP transport. A split size header, a packet split across
messages, and multiple packets in one message are supported. A WebSocket message
is not assumed to be an Aether packet.

`Protocol::kWebSocket` is `ws://`; the appended
`Protocol::kWebSocketSecure` is `wss://`. Existing TCP/UDP/WS enum values are
unchanged. Endpoint hostnames remain intact for DNS, TLS SNI and certificate
hostname verification. IPv6 literals use bracketed eight-group addresses.

Browser builds use Emscripten's WebSocket API (`-lwebsocket.js`) directly,
without a TCP proxy. The browser validates WSS certificates. Use
`config/user_config_web_hydrogen.h` or `config/user_config_web_sodium.h` as an
absolute USER_CONFIG path. The default browser registration endpoint is
`wss://dbservice.aethernet.io:9013/`. Work-cloud WebSocket endpoints must be
advertised by the server; no TCP-port arithmetic or guessed host mapping is used.

Native WebSockets are opt-in with `-DAE_NATIVE_WEBSOCKET=ON`. On Apple this selects
the C API of Network.framework (macOS 10.15 / iOS 13 or newer), using system DNS,
TLS and certificate/hostname verification. No Swift or Objective-C++ bridge is
needed; the Apple source uses Clang blocks. Other native platforms retain pinned
IXWebSocket and mbedTLS sources:
Windows uses its root store, Linux its installed CA bundle, Android the public
Conscrypt/system trust directories. Missing trust anchors fail the connection.
Builds with native WebSockets disabled do not advertise WebSocket channels.

Runtime transport handles, callbacks and bounded mailboxes are not persistent
objects. Platform callbacks never access the model/domain. A model-scheduler
task drains received bytes and publishes stream state/data. Closing removes
browser handlers or joins the IXWebSocket I/O thread before destroying callback
storage. On Apple, cancellation is asynchronous: Dispatch callbacks retain
detached runtime state, never the backend or model. Outstanding send/receive
callbacks finish safely after backend destruction. Channel/server connection
owners perform reconnect by creating a new
transport; old partial packets are discarded. Writes report local acceptance,
not peer delivery (as with TCP). Higher-layer SafeStream/SharedNode ACKs remain
responsible for reliable message delivery.

Maximum logical packet size is 1 MiB; transmit/receive mailboxes are bounded at
8 MiB. An untrusted oversized length is rejected before allocating its payload.
The normal stream_info()/write-action contract exposes readiness/failure and
backpressure. These limits are transport limits, not measured throughput claims.
The Apple backend also limits Network.framework's WebSocket message reassembly
to 8 MiB, automatically answers pings, and counts queued/in-flight sends until
the system reports completion. A send completion does not acknowledge delivery.

`-DAE_BUILD_WEBSOCKET_PROBE=ON` builds the real-server framing probe.
Validation results are recorded separately; compilation alone is not evidence
of registration, peer exchange, restart, or browser persistence.

On Apple, `test-apple-websocket` exercises the real backend against a loopback
RFC 6455 server. Its Python standard-library fixture uses `openssl` to generate
an untrusted TLS certificate; it never modifies the machine's trust store.
Run it with `ctest --test-dir <build> -R 'test-(transport|apple-websocket)$'
--output-on-failure`. This covers binary buffer ownership, continuation frames,
ping/pong and the close handshake, size limits, refused connections, failed
upgrade/TLS verification, and cancellation with send/receive callbacks outstanding.

For native WSS-only cloud validation, configure a separate build with
`-DAE_NATIVE_WEBSOCKET=ON -DAE_BUILD_WEBSOCKET_PROBE=ON` and
`-DUSER_CONFIG=<absolute-source-path>/config/user_config_web_hydrogen.h`.
Those transport flags also work on native builds and disable TCP/UDP fallback.
Run the built `aether-websocket-probe` and `aether-websocket-client-probe`
executables. Both use explicit WSS registration; the
client probe also connects to an advertised work server. Native builds with both
TCP and UDP disabled also default to WSS registration, allowing the ordinary
cloud and A/B exchange examples to validate WSS and persisted-state restart.
For that restart check, use `-DAE_DISTILLATION=OFF -DAE_FILTRATION=ON` so the
second run actually loads the saved objects.
Other native builds retain their existing TCP registration defaults.

Validation on 2026-11-13: Windows MSVC x64 Debug transport tests passed, including
fragment/coalescing, oversized packet length, backpressure, final packet before
close, and synchronous release of the transport by an event handler. Scoped
server-connection and adaptive-datagram regressions passed. The native framing
probe received the signed-key notification over verified production WSS.
Visible Chromium ran the compiled C++/WebAssembly client, registered through
WSS port 9013, and logged in to advertised work-server WSS endpoints on port
9023 (dbservice, los and helsinki). TCP/UDP are disabled in that browser build.
This is functional transport validation, not an online Presence benchmark.
Peer-message/restart acceptance belongs to the dependent chat application.

Apple validation on 2026-11-13: macOS 13.7.8 x86_64, MacPorts Clang 20.1.8,
Debug with AddressSanitizer, native WebSockets enabled, TCP/UDP disabled, and
filtration enabled. The transport, Apple-backend and server-connection CTest
suites passed. Production WSS returned the 99-byte signed-key notification,
registered a client on port 9013, and connected to advertised work-server WSS
endpoints on port 9023. Both the A/B exchange and cloud examples exited zero
with fresh state and again after restarting with saved state. A/B client IDs
were preserved, with two messages delivered in each direction on both runs.
No AddressSanitizer errors were reported. The final backend also passed strict
compilation checks for iOS arm64 and x86_64 Simulator with SDK 17.2 and an iOS 13
deployment target; iOS runtime execution was not tested. Compiler/SDK paths and
local build settings are not part of the project or CI configuration.
