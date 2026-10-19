# Server UDP datagram coalescing (>1200 B budget)

Sources: **`defaae3e566dc901c8266c7120c74f4ddf70a44f`** at
`C:\Users\nickc\Projects\aether-server-ref`.

## Evidence classes

| Class | Meaning in this document |
|-------|---------------------------|
| **Cloud observation** | Client stream-matrix audit on real route (artifacts under `artifacts/`, not in git) |
| **Local test** | C++ loopback / unit tests in `aether-client-cpp` |
| **Source chain** | Call graph verified in Java @ `defaae3e` |
| **Build-time config** | Gradle dependency wiring in server repo |
| **Deployed runtime** | **UNKNOWN** — no production packet capture or ops confirmation in this task |

## Cloud observation (not re-run this iteration)

- Four-block repro (`4×1024 B`, `out=4`): with Windows recv **2048 B** → **`WSAEMSGSIZE`** (no userspace RX).
- Same repro with bench recv **65536 B** → app delivery OK, **`max_rx_payload` up to 4411 B**, **`rx_violations > 0`**, client TX ≤1200 B.
- Artifacts: `artifacts/stream_udp_four_block_20261019/`,
  `artifacts/stream_udp_four_block_recv64k_20261019/`.

This proves **oversized server→client UDP** when the stack accepts the datagram. It does **not** alone identify which JVM FastMeta backend is live in production.

## Build-time FastMeta backend (source repo)

- `server-core/build.gradle`: `api project(':net-virtual')` — cloud server **links VirtualFastMetaNet** at build time.
- `net-virtual/.../VirtualFastMetaNet.java` static initializer:
  `FastMetaNet.INSTANCE.set(new VirtualFastMetaNet())`.
- Alternate backends exist but are separate modules:
  - `net-netty` → `NettyFastMetaNet` (also sets `INSTANCE` if loaded)
  - `net-nio` → `NioFastMetaNet`
- **Which `INSTANCE` wins at runtime** depends on classpath / static init order → treat **deployed backend as UNKNOWN** unless confirmed operationally.

## Chain A — Virtual UDP server (client ↔ cloud login/work socket)

**Scope:** Incoming client datagram to cloud; outbound reply on same virtual UDP session.  
**Not** inter–work-server P2P (`P2PNet` / `MyWorkServerP2PRootApi`).

### A.1 Backend selection & UDP socket

1. `AetherCloudServer.startWorkServer()` → `FastMetaNet.INSTANCE.get().makeServer(listenUri, …)`  
   `server-cloud/.../AetherCloudServer.java` ~68–83.
2. `VirtualFastMetaNet.makeServer` → `new VirtualFastMetaServer<>(…)`  
   `net-virtual/.../VirtualFastMetaNet.java` ~17.
3. `VirtualFastMetaServer` constructor: scheme `udp` → `startUdpServer()`  
   `VirtualFastMetaServer.java` ~47–62, ~65–124.

### A.2 Inbound datagram → MetaContext

4. UDP thread: `DatagramSocket.receive(packet)`  
   `VirtualFastMetaServer.startUdpServer()` ~76–77.
5. Per datagram virtual thread: `new UdpTransport(socket, sender)`  
   ~86.
6. `PooledAutoFlushContext ctx = …` + `contextConfigurator.apply(ctx)` → e.g. `LoginApiImpl`  
   ~87–98, ~109 (`localApiMeta.makeLocal(ctx, dataIn)`).

### A.3 Outbound queue → plaintext batch

7. API handlers enqueue **`LazyPacket`** instances on `MetaContextBase.toRemote` (via normal FastMeta API writes).
8. `AutoFlushContext` timer / `notifyDataAdded` → `MetaContextBase.flush()`  
   `fast-meta/.../AutoFlushContext.java` ~96–100, ~36–39.
9. `MetaContextBase.runFlushLifecycle()` → registered **`flushAction`**  
   `MetaContextBase.java` ~205–223.
10. **`MetaContext.onFlushData` default** (`MetaContext.java` ~692–700):
    - `onFlush(() -> { … var d = remoteDataToArray(); c.accept(d); })`
11. **`MetaContextBase.remoteDataToArray(DataOut)`** (~524–529): **poll all `toRemote` packets**, each `packet.serialize(out)` **with no datagram size cap**.

**Earliest level with whole protocol units before merge:** **`toRemote` queue** (each `LazyPacket` before step 11 drains the queue).

### A.4 Encryption / framing / UDP send (Virtual)

12. Virtual server registers: `ctx.onFlushData(d -> transport.send(d))`  
    `VirtualFastMetaServer.startUdpServer()` ~99–104.
13. For TCP-style contexts: `ctx.onFlushData(data -> transport.send(data))` after API setup  
    ~191–199 (`createContext`).
14. **`UdpTransport.send(byte[])`** — single `DatagramPacket` per call, no split  
    `net-virtual/.../UdpTransport.java` ~56–58.

**Note:** Step 10–12 passes **already concatenated plaintext** from step 11. Any encryption/framing applied inside API/converters runs **before** bytes reach `onFlushData` (handler-specific). The coalescing boundary is **`remoteDataToArray()`**, not the UDP syscall.

## Chain B — Netty UDP (same SHA, separate implementation)

Documented for completeness; **do not merge with Chain A**.

1. `NettyFastMetaNet.makeServer` → `NettyFastMetaServer.startUdpServer()`  
   `net-netty/.../NettyFastMetaServer.java` ~188–198.
2. `UdpServerHandler.channelRead` → `NettyServerConnectionHandler.udpInit()` + `channelRead`  
   ~303–326.
3. **`NettyServerConnectionHandler.handleFlush`** (~216–250): `context.remoteDataToArray(combinedData)` → one `DatagramPacket`.

**Source-only finding @ `defaae3e`:** `initializeApis()` does **not** call `context.onFlush(handleFlush)`; `handleFlush` is **private** and **no references** in `net-netty`. `channelRead` has **`makeLocal` commented out** (~180). So this Netty UDP path appears **incomplete / not wired** at this SHA — **exclude from “proven runtime chain”** unless a different revision is deployed.

## Chain C — P2P between work servers (not client↔cloud repro)

`P2PNet.startP2PAcceptor()` → `fastMetaNet.makeServer(p2pListenUri, WorkServerP2PRootApi…)`  
`server-cloud/.../P2PNet.java` ~465–475.

Outbound relay: `MyWorkServerP2PRootApi.sendEncryptedPayload` → `engine.encrypt(data)` → `remoteRoot.enter(P2PStream.Out.send(encrypted))`  
~548–607.

This is **server↔server P2P**, not the client matrix cloud login path. Client oversized RX may still involve **cloud client API relay** (`AuthorizedApiImpl.client` → `onFlushData` → `clientInteraction`) which again uses **`onFlushData` → `remoteDataToArray()`** pattern (`AuthorizedApiImpl.java` ~1416–1431).

## Fix plan (server — not applied here)

**Earliest safe hook:** before or inside step 11 — drain **`toRemote`** in **whole `LazyPacket` units**, estimating **post-framing + post-encryption size**, emitting **multiple self-contained datagrams ≤ budget** per flush cycle.

Requirements:

- Each datagram independently decodable on the client.
- Never truncate a protocol unit; never split arbitrary ciphertext.
- No reliance on IP fragmentation.
- TCP flush/coalescing unchanged.

If one atomic unit exceeds budget after all wrappers, document existing size contract or fragmentation — **no new wire protocol in the client bench**.

## Verification criteria (post-fix server)

- Cloud four-block @ **2048 B** recv: no EMSGSIZE, `max_rx ≤ 1200`, `rx_violations = 0`.
- Client `EvaluateUdpBudget` → **COMPLIANT** on both TX and RX observations.
- Virtual/Netty unit tests: N small packets → M datagrams with bounded size on wire.
