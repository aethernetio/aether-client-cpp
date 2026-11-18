# Native peer transport, version 1

`Client::ConfigurePeer()` enables LAN discovery and authenticated direct IPv4
UDP. Existing `P2pStream`, `P2pSafeStream` and `P2pAdaptiveDatagramStream` use the
transport through `P2pStream`; their acknowledgement, ordering, retry and delivery
semantics remain in those existing classes. The transport does not implement a
second reliable messaging protocol. Server code and database schemas are unchanged.

## Configuration and use

```cpp
ae::PeerSettings peers;
peers.app_id = app_id;                 // Shared by instances of this application.
peers.services = {{storage_service_id, 1}};
peers.discovery = true;
peers.discovery_port = 46000;          // Common discovery port for the application.
peers.listen_port = 0;                 // OS-selected port per local interface.
peers.direct_peers = {{remote_uid, {remote_ipv4, remote_port}}};
client->ConfigurePeer(peers);

ae::StreamDefinition control;
control.id = 10;                       // Same logical ID at both endpoints.
control.name = "control";
control.destination = remote_uid;
control.service = {storage_service_id, 1};
control.routing = ae::StreamRoutePolicy{true, true, true, true};
ae::StreamDefinition files;
files.id = 11;
files.name = "files";
files.delivery = ae::StreamDelivery::kReliable;
control.children.push_back(files);
client->ConfigureStreams({control});
client->streams().Find(11)->Write({1, 2, 3});
```

Subscribe to the normal stream data/update events and retain their `Subscription`
objects. An application can instead continue using the original flat stream API
(logical ID zero). Unknown incoming stream IDs use the existing manager's
`new_port_event()` acceptance path. A port binds `(remote UID, stream ID)` to a
service ID/version; a conflicting service is rejected. Discovery and service
advertisements describe capabilities, not authorization to read or write storage.
The application must enforce permissions for its service.

`StreamHierarchy` owns `HierarchicalStream` nodes. Each node delegates to an
existing base stream. Children inherit an omitted destination, routing policy and
service from their parent, but have independent logical IDs, delivery state and
traffic counters. The parent itself can carry data. IDs must be nonzero and unique
within a client's hierarchy; a hierarchy has at most 64 nodes and depth 16.
Explicit routing replaces the entire inherited routing policy. Reliability is
selected per node using `kMessages`, `kReliable` or `kAdaptiveDatagram`.

`AE_SUPPORT_P2P=0` excludes native direct transport; `AE_SUPPORT_LAN=0` keeps
explicit direct-IP support but excludes multicast discovery. Configure these in
the normal CMake `USER_CONFIG` header. `AE_LAN_DISCOVERY_PORT` and
`AE_LAN_LISTEN_PORT` change defaults. Native support requires IPv4, UDP and a
complete existing Sodium or Hydrogen crypto configuration. It is enabled by
default on Windows, Linux and macOS, and disabled on other targets. This change
does not provide native peer sockets for ESP-IDF or browsers.

## Discovery, routes and recovery

Discovery uses IPv4 multicast `239.255.46.1`, with TTL 1, every two seconds on
every active local IPv4 address. The shared discovery listener joins each local
interface. The announcement contains protocol version, crypto profile, AppId, a
random runtime instance token, listening port and service ID/version list. It
contains no client UID or client signing key. Clients with a different AppId or
crypto profile are ignored. Both applications must support a common compiled
crypto profile; this version does not negotiate between Sodium and Hydrogen.

The registry separates anonymous, untrusted `candidates()` from authenticated
`clients()`. An authenticated client's services are refreshed through encrypted
probes. `changed_event()` announces discovery, authentication, health and probe
updates. Candidate advertisements expire after eight seconds; the runtime registry
also retains a bounded history of disconnected authenticated paths for statistics.

Each candidate endpoint and each configured direct endpoint is attempted from
all local IPv4 interfaces. Endpoint pairs have independent sessions and health.
Routing priority is LAN, explicit direct IP, then the existing server transport.
Discovery and connection attempts run alongside server delivery: the first message
does not wait for LAN discovery. Once an authenticated direct route is available,
normal application payloads use that route exclusively. A message already handed
to a server is not recalled. A common delivery envelope and receive window prevent
duplicate delivery across route transitions. There is no unconditional duplicate
send of every payload through all transports.

Within a route class, measured RTT selects the path initially. After eight probe
outcomes the score is `RTT / max(0.05, 1 - recent loss)`, using the last 16 outcomes.
Probes use the existing `ae::Ping` lifecycle and user-configured RX timings.
Confirmed schedules and the user's offline timeout determine presence; there is
no independent one-second/four-second timer. See [client_presence.md](client_presence.md).
RTT and jitter use moving averages. An immediate UDP send failure tries another authenticated path in the
same write, then permits the cloud fallback. A silent blackout is detected by
probes; raw datagrams already accepted by the OS may be lost. Existing reliable
streams perform their own retransmission and can continue over the next route.
No successful OS send is presented as an end-to-end delivery acknowledgement.

Direct addresses are retried every two seconds and interfaces are rescanned every
ten seconds. Interface removal retires its paths; interface arrival binds a new
socket and rejoins multicast. Handshake flights retry every 250 ms, with a
five-second attempt lifetime. Paths, candidates and history have bounded capacity.
For a saved direct endpoint, configure a stable listening port at the remote
client or update the endpoint when it changes. An OS-selected port changes after
restart; LAN discovery can learn that new port. There are at most 64 concurrent
direct paths, 128 discovery candidates and 128 historical authenticated paths.

`keep_backup_channels=true` retains alternative direct paths and the destination
cloud connection. With it disabled, a stream releases its destination cloud
connection when direct delivery is ready, and only the best measured direct path
is actively probed after the initial measurement period. A different session used
by the remote side remains valid while it receives authenticated traffic; unused
sessions expire normally. This permits asymmetric choices without breaking the
other side's selected path. Direct backup demand is shared
by all streams to that peer: any stream requesting backups keeps them alive.
The client's own cloud listener/control connection is independent of this flag.

UDP hole punching and the AeroAdmin-style dual-socket strategy are described in
[rendezvous.md](rendezvous.md). Their confirmed paths share this transport and have priority
after direct IP and before cloud delivery. Discoverable local endpoints and saved
reachable direct endpoints work without Internet after credential provisioning.

## Identity and trust

The signing key pair is generated on the client. The existing authenticated
`AuthorizedApi::RequestSignedUid(vector<Key>)` (ID 50) requests an attestation;
`ClientApiSafe::SendSignedUid(keys, signatures)` (ID 71) receives it. The server
selects UID from the authenticated session and signs the SDK serialization of
`UID || KeySignPublic` (16-byte UID, one key-type byte, 32 key bytes for both
currently supported profiles). The client does not send its private signing key
to the server. Root-signed server signing keys from registration/cloud resolution
are retained in `Server` and copied into the peer certificate.

Verification follows `trusted Æthernet root -> server signing public key ->
UID + client signing public key`. The root verifies the raw server public-key
bytes, matching the existing server format. The server key verifies the exact
serialized UID/key binding. The peer then proves possession of the corresponding
private key in the handshake. An attestation alone is insufficient to impersonate
that UID.

The server's method permits multiple signing keys for the same UID; there is no
global uniqueness, expiry or revocation claim. `RenewPeerIdentity()` creates a new
pair and requests another attestation; an old valid attestation remains valid.
Without an additional online revocation protocol an offline peer cannot learn
that a previously issued credential should be distrusted. Direct-IP configuration
also pins the expected UID. Discovery authenticates the UID that responds; an
application connecting to a particular service must apply its own UID/access rules.

## Handshake and records

All packets use the existing SDK API packer and binary serializer, with checked,
bounded parsing. The common header contains magic `AEP1`, version 1, AppId, a
random session token and crypto profile. The native wire uses the SDK's existing
little-endian representation. Header and all key-exchange packets are hashed into
the signed handshake transcript.

1. Initiator sends `Hello` (ID 4), containing its ephemeral key-exchange packet.
2. Responder sends `Reply` (5), containing its ephemeral key-exchange packet.
3. Initiator sends `Finish` (6), containing the final exchange packet and its
   encrypted certificate and proof of signing-key possession.
4. Responder verifies that proof and sends `Authenticate` (7), containing its
   encrypted certificate and possession proof.
5. Initiator verifies it and sends an encrypted `Confirm` record.
6. Responder verifies confirmation, sends its encrypted confirmation, and becomes
   ready. Initiator becomes ready after receiving this confirmation.

The proof signs a domain string, transcript hash, initiator/responder role and
the sender's full certificate. Confirmation hashes the transcript and both
certificates in initiator/responder order, binding both identities to this session.
Exact retransmitted handshake flights return cached identical responses.

Sodium uses `crypto_kx` with fresh ephemeral key pairs and directional session
keys, Ed25519 identity signatures and ChaCha20-Poly1305-IETF records. The nonce
contains a monotonically increasing 64-bit sequence followed by zero bytes; the
sequence is authenticated as associated data. Hydrogen uses `hydro_kx_XX`, with
fresh per-session key pairs for its static-key inputs, Hydrogen signatures and
`hydro_secretbox` records with the same sequence as message ID and context
`aepeer01`. No persistent identity key is used as a key-exchange secret. Both
profiles erase ephemeral exchange state after deriving session keys and erase
session cipher keys at destruction. Existing dependency pins are unchanged.

Record ID 8 carries encrypted session messages: `Confirm` (3), `Data` (4),
`Ping` (5) and `Pong` (6). Each direction has its own key and sequence. The
64-record receive window permits out-of-order traffic and rejects replay.
Authentication failure never advances that window. Logical delivery uses a
separate 256-message window per sender/runtime epoch across all routes; it does
not implement delivery confirmation or replace base-stream retries.

UIDs, identity signing public keys, certificates and payloads are encrypted on
native peer links. AppId, service advertisements, IP addresses, timing and lengths
are observable. This is passive UID privacy: an active peer can initiate a
handshake and learn the other peer's identity. An active intermediary can also
learn the initiator's identity before being rejected, but cannot complete a
session as a different certified UID without its private key. There is no claim
of resistance to LAN traffic analysis or resource-exhaustion attacks. Mutual
authentication is required before application payloads are accepted.

The primitives follow the existing libraries' APIs:
[libsodium key exchange](https://doc.libsodium.org/key_exchange),
[libhydrogen key exchange](https://github.com/jedisct1/libhydrogen/wiki/Key-exchange).
Protocol tests are not an independent cryptographic audit.

## Persistence and statistics

`Client` persists peer settings, signing identity/certificate and stream
definitions through its normal domain save path. `Server` persists root-signed
signing keys. The application must save after credential issuance or configuration
changes, as with other SDK state. On load, runtime peer transport and configured
stream hierarchy are rebuilt automatically. No migration reader for older state
is provided, as agreed for this unused library.

Sockets, sessions, discovery candidates, connections, replay windows, pending
writes, reliable-stream runtime state and statistics are runtime only. Loading
recreates those resources and establishes fresh sessions. Persisted credentials
allow verification without calling Æthernet again; an uncertified client still
needs an online provisioning step. Existing prepared packet blocks also include
a random delivery epoch and counter. `EncodePacket` remains an explicitly
server-bound packet encoder; callers using it directly choose the UDP endpoint.
Normal stream users receive automatic route selection.

The desktop default event pool is 1024 events, allowing cloud connections and
stream hierarchies to coexist. Other platforms retain 256; an application can
override the existing `AE_EVENTS_MAX_COUNT` setting for its resource budget.

`HierarchicalStream::statistics()` and `P2pStream::statistics()` return entries
indexed by `PeerRoute`: cloud, direct IP, LAN, hole punching. Message and byte counts are payload
handoffs at the base stream, and accepted incoming messages after common
deduplication. Reliable/adaptive protocol data and retransmissions at that base
layer therefore count as traffic; these are not application transaction counts.
Wire counters and probe statistics on direct entries describe the shared peer
paths, including framing, crypto and control traffic but excluding IP/UDP headers.
They must not be added across sibling streams. Direct RTT measures a peer round
trip; cloud RTT measures the connected server, not an end-to-end peer round trip.
Cloud wire/chunk counters by actual server endpoint are exposed separately by
`ServerConnection::transport_statistics()`. Statistics are runtime snapshots,
not a durable accounting ledger. `BestPath()` identifies the active direct path.

`OfflineStorageSettings` reserves provider UID, service and
`fetch_pending_on_client_load` (default true) as persistent stream intent. Storage
is an explicit stream option, never an automatic network fallback. Live stream
creation with a storage option fails explicitly until a provider is implemented.
The existing load-to-hierarchy restoration point is where that future provider
must automatically request pending data. This change does not fetch or store data.

`peer-lan` is a runnable provisioning/LAN example. Run it once online in a fresh
working directory, then rerun `peer-lan --offline` with the saved `state` directory.
It exchanges reliable data in both directions and checks zero cloud payloads.
Build this persistence example with `-DAE_FILTRATION=ON` (or production loading
after provisioning). Pure `AE_DISTILLATION=ON, AE_FILTRATION=OFF` deliberately
erases state at startup throughout the existing SDK and cannot run offline after
a restart.
