# Emscripten Browser Transport Design

Status: design + implementation plan for `feature/emscripten-browser-transport-v1`  
Base commit: `0b0e3b54b9ffa730c41597c8b18f6a75255bded3`  
Primary sources consulted:

- https://emscripten.org/docs/porting/networking.html
- https://emscripten.org/docs/api_reference/Filesystem-API.html
- https://emscripten.org/docs/api_reference/fetch.html
- https://github.com/jedisct1/libsodium.js
- https://developer.mozilla.org/en-US/docs/Web/API/WebSocket
- https://developer.mozilla.org/en-US/docs/Web/API/Fetch_API
- https://developer.mozilla.org/en-US/docs/Web/API/Web_Crypto_API
- https://developer.mozilla.org/en-US/docs/Web/API/IndexedDB_API

Pinned dependency versions and licenses are recorded in §4 and in
`examples/browser_ping_pong/vendor/`.

---

## 1. Current transport creation chain

Verified against source:

```
AetherAppContext::DefaultAdapterFactory
  -> EthernetAdapter
       -> EthernetAccessPoint::GenerateChannels(server)
            filters AddrVersion::{kIpV4,kIpV6,kNamed}
            filters Protocol::{kTcp,kUdp} only
            -> EthernetChannel(endpoint)
                 -> TransportBuilder()
                      -> EthernetTransportFactory::Create
                           Protocol::kTcp -> TcpTransport<Win/Unix/LwipSocket>
                           Protocol::kUdp -> UdpTransport<...>
                           default -> assert(false)
```

Key files:

- `aether/adapters/ethernet.*`
- `aether/access_points/ethernet_access_point.*`
- `aether/channels/ethernet_channel.*`
- `aether/channels/ethernet_transport_factory.*`
- `aether/transport/system_sockets/tcp/tcp.h`
- `aether/stream_api/istream.h` (`ByteIStream`)
- `aether/write_action/*`

`ByteIStream` contract used by all transports:

- `Write(DataBuffer&&)` → `WriteAction&` (terminal Success/Fail/Stop)
- `stream_update_event()`, `stream_info()`, `out_data_event()`, `Restream()`
- `LinkState::{kUnlinked,kLinked,kLinkError}`

TCP writes length-prefix packets via `VectorBuffer` + `PacketQueueManager`.
Browser transports must preserve the same packet framing semantics expected by
upper layers (length-prefixed reliable byte stream for TCP-like channels).

There is **no** `__EMSCRIPTEN__` platform path today. Native sockets, c-ares,
and OS pollers are the only networking implementations.

### Selected architecture (do not overload EthernetAdapter)

```
BrowserAdapter
  -> BrowserAccessPoint
       -> BrowserChannel
            -> BrowserTransportFactory
                 -> BrowserWebSocketTransport  (ws:// / wss://)
                 -> BrowserHttpTransport       (http:// / https://)
                      -> ByteIStream
```

Browser networking uses browser-native APIs only:

- HTTP/HTTPS: Fetch API / `emscripten_fetch`
- WS/WSS: WebSocket API / `<emscripten/websocket.h>` + `-lwebsocket.js`

Rejected for production:

- POSIX TCP sockets compiled into WASM
- OpenSSL / WinHTTP / c-ares in the Emscripten build
- WebSockify / `PROXY_POSIX_SOCKETS` (temporary comparison only, labelled)

---

## 2. Current Endpoint / Protocol serialized representation

### Protocol enum (`aether/types/address.h`)

| Value | Enumerator | Status |
|------:|------------|--------|
| 0 | `kTcp` | implemented |
| 1 | `kUdp` | implemented |
| 2 | `kWebSocket` | enum only; marked unsupported |
| — | `kHttp` / `kHttps` | commented out (not present) |

**Rule:** never renumber existing values. Append only.

### Appended values for this milestone

| Value | Enumerator | Meaning |
|------:|------------|---------|
| 3 | `kHttp` | insecure HTTP tunnel |
| 4 | `kHttps` | secure HTTP tunnel |
| 5 | `kWebSocketSecure` | `wss://` |

`kWebSocket` (2) means insecure `ws://`.

### Address / Endpoint wire layout

`Endpoint` reflects `AddressPort` + `protocol`:

- `Address` = `VariantType` indexed by `AddrVersion` (`kNull=0`, `kIpV4=1`,
  `kIpV6=2`, `kNamed=3`)
- then `uint16 port`
- then `uint8 protocol`

Existing persisted TCP/UDP blobs remain readable because values 0/1 and
AddrVersions 1–3 are unchanged.

### Browser endpoint representation (versioned)

Classic `Endpoint` cannot carry path or gateway target. Add:

```text
AddrVersion::kBrowser = 4
BrowserAddr v1 {
  uint8  representation_version = 1
  string hostname
  string path                 // e.g. "/aether/v1/ws"
  string gateway_target       // opaque allowlisted destination id
}
```

Serialized via AE_REFLECT. Combined with `Endpoint.port` + `Endpoint.protocol`
this yields an explicit scheme/host/port/path/target without stuffing URLs into
`NamedAddr`.

### Registration / cloud advertising

Today registration cloud hard-codes TCP `:9010`
(`aether/aether_app.cpp`). Work clouds advertise TCP/UDP only.

**Milestone 1:** runtime browser endpoint override from the page UI
(gateway URL + transport scheme). If the loaded cloud has only raw TCP/UDP and
no browser mapping, the UI reports that explicitly.

**Production design:** registration and work clouds must advertise
`BrowserAddr` endpoints (or dual-stack TCP + browser) so clients do not need a
page override. That is a server/cloud configuration change, not a silent client
rewrite of TCP endpoints.

---

## 3. Current crypto primitive inventory

Default config (`aether/config.h`):

| Concern | Macro | Implementation |
|---------|-------|----------------|
| PoW | `AE_BCRYPT_CRC32` | `bcrypt_hashpw` then CRC32 of bcrypt string |
| Signature | `AE_ED25519` | `crypto_sign_verify_detached` |
| Async crypto | `AE_SODIUM_BOX_SEAL` | `crypto_box_seal` / `crypto_box_seal_open` |
| Sync crypto | `AE_CHACHA20_POLY1305` | **original** `crypto_aead_chacha20poly1305_*` (8-byte nonce) |
| KDF | `AE_SODIUM_KDF` | `crypto_kdf_derive_from_key`, context `"_aether_"` |
| Random | sodium | `randombytes_buf` |

Wire format constraints that must be preserved exactly:

- ChaCha20-Poly1305 **original** (not IETF / not XChaCha20)
- Nonce length 8; ciphertext layout `ciphertext ‖ tag ‖ nonce`
- Sealed-box overhead `crypto_box_SEALBYTES` (48)
- Sodium KDF context `"_aether_"` (8 bytes) and subkey IDs
- Ed25519 key/signature formats
- bcrypt salt/version/output string behavior used by PoW

Pinned native CPM tags (resolved at configure time):

| Library | GIT_TAG | Resolved (approx) | License |
|---------|---------|-------------------|---------|
| libsodium | `master` | 1.0.19 (`403efe6…`) | ISC |
| libbcrypt | `master` | `8aa32ad…` | public-domain / permissive |
| libhydrogen | `bbca575` | `bbca575…` | ISC |

Hydrogen is **not** used in the default browser configuration and must not be
linked into the default browser binary.

---

## 4. Selected browser crypto implementations

| Primitive | Browser backend | Reason |
|-----------|-----------------|--------|
| ChaCha20-Poly1305 original | **libsodium.js** (sumo or standard build that exports original AEAD) | WebCrypto has no original ChaCha20-Poly1305 |
| crypto_box_seal / open | **libsodium.js** | WebCrypto has no sealed-box |
| crypto_kdf_derive_from_key | **libsodium.js** | HKDF ≠ sodium KDF |
| Ed25519 verify | **libsodium.js** (WebCrypto optional later after KATs) | Prefer sync API matching Aether providers |
| CSPRNG | `crypto.getRandomValues` and/or `sodium.randombytes_buf` | WebCrypto allowed for random only |
| bcrypt_hashpw | Prefer audited JS port with byte-identical vectors; fallback: compile pinned `libbcrypt` into WASM | Never substitute Argon2/PBKDF2 |

Pinned browser packages (vendored, no CDN at runtime):

| Package | Version | License |
|---------|---------|---------|
| `libsodium-wrappers` / `libsodium` (js) | **0.8.4** | ISC (same family as libsodium) |
| bcrypt web port | selected after KAT pass; else WASM libbcrypt | documented in vendor lockfile |

Rejected alternatives:

- AES-GCM for ChaCha20-Poly1305 — different algorithm/wire format
- HKDF for `crypto_kdf_derive_from_key` — different construction
- PBKDF2/Argon2 for bcrypt — PoW must match server
- Raw X25519 for `crypto_box_seal` — missing ephemeral+MAC framing
- CDN-loaded crypto scripts — supply-chain / CSP / offline static hosting

Initialization: `await sodium.ready` before constructing `AetherApp`.
Narrow typed JS↔C++ bridge; no arbitrary memory export.

---

## 5. Storage lifecycle

Preferred approach **A**: existing `FileSystemStdStorage` over an IDBFS mount.

Lifecycle:

1. Parse `?profile=` → isolated mount path `/aether/<profile>/`
2. `FS.mkdir` + `FS.mount(IDBFS, {}, path)`
3. `FS.syncfs(true, …)` **before** `AetherApp::Construct`
4. Use synchronous file I/O semantics of `FileSystemStdStorage`
5. After domain `Save`, debounce `FS.syncfs(false)`
6. Explicit flush after registration and other critical transitions
7. Best-effort flush on `visibilitychange` / `pagehide` (not only `beforeunload`)
8. Optional `navigator.storage.persist()`; always report result in UI

Concurrency: Web Locks (`navigator.locks`) or BroadcastChannel hard-error so two
tabs cannot write the same profile.

Not authoritative for Aether state: HTTP cache, CacheStorage, localStorage,
cookies. CacheStorage may cache static assets only.

`AE_FILE_SYSTEM_STD_ENABLED` currently keys off desktop OS macros and would
accidentally enable under Emscripten via `__unix__`/`__linux__`. The Emscripten
path must mount IDBFS first and set the working directory; document that
native-only assumptions in aether-objects may need a thin wrapper.

---

## 6. Browser event-loop lifecycle

Single-threaded WASM. No pthreads, SharedArrayBuffer, COOP/COEP, or
`PROXY_TO_PTHREAD` for milestone 1.

Do **not** call blocking `WaitActions` / `WaitUntil` / `sleep` on the browser
main thread. `ManualTaskScheduler::WaitUntil` uses `condition_variable` and is
incompatible with cooperative browser scheduling.

Cooperative loop:

```text
schedule(now):
  next = AetherApp::Update(now)
  delay = clamp(next - now, 0, max_slice)
  setTimeout/rAF(schedule, delay)
```

Async startup state machine:

```text
LoadingStorage -> LoadingCrypto -> ConstructApp
  -> SelectClient/Register -> OpenP2pStream -> Ready
```

`IPoller`: provide `BrowserPoller` (no file descriptors). Browser network
callbacks enqueue work onto the Aether scheduler; they must not invent fake fds.

No c-ares in Emscripten builds. Hostname resolution and TLS are browser-owned.

Asyncify: disabled unless a measured, documented requirement remains after the
event-loop refactor.

---

## 7. HTTP tunnel protocol

No existing Aether HTTP tunnel was found in this repository.

**Test-only opaque gateway** (not a production claim):
`tools/browser_transport_gateway/`

Versioned contract **v1**:

| Method | Path | Body |
|--------|------|------|
| `POST` | `/aether/v1/connect` | optional JSON `{ "target": "<allowlisted-id>" }` → `{ "session": "<opaque>" }` |
| `POST` | `/aether/v1/session/{id}/send` | `application/octet-stream` payload |
| `GET`  | `/aether/v1/session/{id}/receive` | long-poll; `application/octet-stream` response |
| `POST` | `/aether/v1/session/{id}/close` | empty |

Properties:

- Opaque byte forwarder only (no Aether decrypt / auth termination)
- Unpredictable session IDs
- Idle expiration
- Bounded request body and queued response sizes
- Bounded concurrent receive requests per session
- Explicit CORS origin allowlist
- Destination Aether TCP servers allowlisted (not an open proxy)
- HTTP errors map to `LinkError` / failed `WriteAction`
- HTTPS uses normal browser certificate validation

Production deployment still requires first-party Aether server endpoints with
the same contract (or a hardened gateway operated by Aethernet). The test
gateway alone does **not** constitute production browser transport support.

---

## 8. WebSocket framing behavior

Path (gateway): `/aether/v1/ws?target=<allowlisted-id>`

- Binary frames only (`binaryType = "arraybuffer"`)
- Arbitrary byte values preserved; no UTF-8 transformation
- Application stream still uses Aether TCP-style length-prefixed packets above
  the raw WebSocket message boundary (one WS message may contain one or more
  stream packets; implementations must not rely on 1:1 message==packet unless
  documented and tested)
- Monitor `bufferedAmount`; apply a bounded high-water mark
- Do **not** mark `WriteAction` success merely because bytes entered an
  unbounded JS queue
- Browser WebSocket control ping/pong is **not** the Aether PING test
- Validate `Origin` on the gateway

---

## 9. Server / gateway changes required

| Component | Change |
|-----------|--------|
| Test gateway | Implement HTTP+WS+TLS frontends forwarding to allowlisted TCP Aether servers |
| Production Aether server | Advertise browser endpoints; optionally host the same opaque tunnel natively |
| Registration cloud | Dual-advertise TCP and browser endpoints for browser clients |
| Work cloud descriptors | Include `BrowserAddr` endpoints where applicable |
| Ops | CORS allowlist, WSS certificates (mkcert for local), destination allowlist |

If the production server repository is unavailable in this workspace, only the
labelled test gateway is implemented here.

---

## 10. Security and browser restrictions

- Mixed content: HTTPS pages must not use `http://` or `ws://` transports
- CORS + Origin checks on gateway
- TLS certificate validation always on in production code
- Development may use mkcert local CA; headless tests may trust that CA explicitly
- Never log private keys, master keys, nonces-with-keys, or decrypted packets
- Zero temporary secret buffers where practical
- No insecure crypto fallback
- No CDN crypto
- IndexedDB protects durability against casual disk loss, **not** against XSS or
  a compromised origin
- Profile isolation + exclusive lock

---

## 11. Test matrix

### Protocol compatibility (native)

- Enum numeric values unchanged for 0/1/2
- Persisted TCP/UDP Endpoint round-trip
- New protocol values + `BrowserAddr` serialize/deserialize

### Browser transport unit tests

- Link state transitions; early write queue; bounded overflow
- WriteAction Success/Fail; binary round-trip including `0x00`/`0xff`
- Fragmented inbound chunks; Restream; close during pending writes
- Post-destruction callback ignore; WS backpressure; HTTP long-poll / expiry

### Crypto KATs

Generate vectors with native C backend; verify browser provider byte-identical:

- ChaCha20-Poly1305 layout; box_seal; KDF; Ed25519; bcrypt+CRC32; malformed inputs

### Persistence

- UID stable across reload / new context with same profile
- Profiles A/B isolated; clear A does not affect B; clear A yields new UID
- No Aether construct before initial `syncfs(true)`

### Real transport matrix

For each of HTTP, HTTPS, WS, WSS:

- register/load → cloud → P2P open → PING/PONG echo → ≥100 periodic exchanges
- reconnect after gateway restart; zero unexplained corruptions

### Browser automation

Playwright (or equivalent): two contexts `?profile=A` / `?profile=B`, DOM/test
API assertions (not screenshots as primary).

### Gateway tests

Invalid Origin/destination, oversized body, unknown/expired session, duplicate
receive, slow client, abrupt disconnect, TCP dest disconnect, CORS preflight,
binary preservation, TLS path.

### Native regression

Existing Aether test suite must remain green after shared changes.

---

## Build commands (target)

```bash
# after emsdk activate
emcmake cmake -S . -B build/emscripten-browser \
  -DAE_BUILD_EXAMPLES=ON \
  -DAE_BUILD_BROWSER_EXAMPLE=ON \
  -DAE_BUILD_TESTS=OFF \
  -DAE_BUILD_TOOLS=OFF \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/emscripten-browser --target aether_browser_ping_pong
```

Linker flags for browser targets are centralized in
`cmake/emscripten-browser.cmake` (`ae_emscripten_browser_link_options`):

- `-lidbfs.js` `-lwebsocket.js`
- `-sFETCH=1` `-sMODULARIZE=1` `-sEXPORT_ES6=1`
- `-sENVIRONMENT=web` `-sALLOW_MEMORY_GROWTH=1`

Commit 1 of this branch adds platform scaffolding (BrowserPoller, IDBFS
helpers, cooperative loop, address types). The `browser_ping_pong` example
target lands in a later commit.

Static output directory (documented):
`build/emscripten-browser/examples/browser_ping_pong/web/`

Contains only static assets: `index.html`, `browser_ping_pong.js`,
`browser_ping_pong.wasm`, vendored crypto JS/WASM, CSS.

---

## Application-level PING/PONG frame (v1)

Endianness: little-endian. Max frame size: 2048 bytes.

```text
magic[4] = 'A','E','P','P'
version  u8  = 1
type     u8  = 1 PING | 2 PONG
session  u32
sequence u64
timestamp_mono_ns u64   # sender monotonic; echoed by PONG
payload_len u16
payload[payload_len]
```

RTT uses monotonic clock only. Malformed frames are counted/rejected without
terminating the app. Bound in-flight pings.

This traffic rides `P2pStream::Write` / `out_data_event` — never raw browser
WebSocket/Fetch as the test itself.
