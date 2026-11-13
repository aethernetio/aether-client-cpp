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

Native WebSockets are opt-in with `-DAE_NATIVE_WEBSOCKET=ON`. CPM supplies pinned
IXWebSocket and mbedTLS sources. Certificate/hostname verification remains on;
Windows uses its root store, Linux its installed CA bundle, Android the public
Conscrypt/system trust directories. Missing trust anchors fail the connection.
Unsupported native builds do not advertise WebSocket channels.

Runtime transport handles, callbacks and bounded mailboxes are not persistent
objects. Platform callbacks never access the model/domain. A model-scheduler
task drains received bytes and publishes stream state/data. Closing removes
browser handlers or joins the native I/O thread before destroying callback
storage. Channel/server connection owners perform reconnect by creating a new
transport; old partial packets are discarded. Writes report local acceptance,
not peer delivery (as with TCP). Higher-layer SafeStream/SharedNode ACKs remain
responsible for reliable message delivery.

Maximum logical packet size is 1 MiB; transmit/receive mailboxes are bounded at
8 MiB. An untrusted oversized length is rejected before allocating its payload.
The normal stream_info()/write-action contract exposes readiness/failure and
backpressure. These limits are transport limits, not measured throughput claims.

`-DAE_BUILD_WEBSOCKET_PROBE=ON` builds the real-server framing probe.
Validation results are recorded separately; compilation alone is not evidence
of registration, peer exchange, restart, or browser persistence.

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
