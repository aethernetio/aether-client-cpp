# UDP rendezvous and hole punching

The SDK uses an ordinary Æthernet client on a public host as rendezvous.
Signaling travels through existing `P2pStream` cloud delivery. UDP source
addresses are discovered by that client directly. This protocol does not use
STUN, TURN or DTLS, and adds no cryptographic dependency or server/database API.

## Configuration

```cpp
ae::PeerSettings settings;
settings.app_id = my_app;
settings.rendezvous = ae::RendezvousSettings{
    rendezvous_uid,
    {public_ipv4, public_udp_port},
    true,   // Standard simultaneous UDP hole punching.
    true};  // Independent pairs with opposite initiators.
client->ConfigurePeer(settings);
```

Creating a normal stream automatically requests connectivity to its destination.
The receiver registers its sockets even without an outgoing stream. Loading the
client restores the configuration and creates fresh runtime registrations.
An omitted rendezvous setting leaves LAN/direct-IP behavior available without
contacting this service. Both endpoints and their rendezvous client must use a
common crypto profile; the example deploys separate Sodium and Hydrogen clients.

`StreamRoutePolicy::hole_punching` is an independent flag, appended to the
previous flags and enabled by default. The common priority is:

1. Authenticated LAN.
2. Direct IP.
3. Authenticated hole-punched UDP.
4. Existing cloud delivery.

Messages use the first available enabled route while connection attempts run in
parallel. After a direct route becomes ready, later payloads use that route.
Shared delivery framing preserves deduplication across transitions. Existing
reliable/adaptive streams keep their own acknowledgement and retry semantics.
`PeerRoute::kHolePunch` has index 3; statistics arrays have `kPeerRouteCount`
entries. `PeerPath::punch_lane` identifies the independent socket pair.

For signaling the SDK reserves logical stream ID `0xfffffff0` and service
`a89d9189-9e39-4a22-ae70-112812720101`, version 2. That stream explicitly allows
only cloud transport, preventing recursive rendezvous attempts for signaling.

## Socket registration

Each active interface has its regular UDP socket and, if dual-socket traversal
is enabled, two additional independent sockets. Registration, NAT-opening
packets, authentication, probes and data for a candidate use the same socket.
The broker never substitutes an endpoint claimed over signaling for an observed
and verified UDP source endpoint.

1. Over authenticated cloud delivery, the client sends AppId, a fresh runtime
   instance ID, crypto profile and a socket ID/lane/local endpoint.
2. Rendezvous binds that enrollment to the authenticated sender UID. It returns
   an opaque token and two independently generated 256-bit directional keys
   through the cloud stream.
3. The client sends an encrypted UDP `Register` record from that socket.
4. Rendezvous sends an encrypted fresh challenge to the observed source.
5. The client returns the encrypted challenge from the same socket.
6. Only then does rendezvous publish that endpoint and acknowledge observation.

UDP registration records use the existing `PeerRecordCipher`, including its
sequence/replay protection. Source endpoint and challenge must match, and a
challenge expires in three seconds. A stolen/replayed UDP packet cannot confirm
ownership of a different source address. Duplicate enrollment returns the same
grant; the recipient ignores a duplicate token rather than resetting its cipher
counter. Renewed grants use fresh tokens and keys. Grant key objects and cipher
keys are erased on destruction. Registration keys are runtime only.

Tokens, rather than UIDs, identify UDP registration records. UIDs remain inside
cloud signaling and the encrypted peer-authentication exchange. The normal SDK
debug logs used on this path record payload sizes instead of raw plaintext
buffers, because signaling includes temporary keys.

## Coordination and the two strategies

Rendezvous pairs only fresh registrations with matching AppId/profile and a
consistent runtime instance on each side. Plans contain observed external and
reported local endpoints. Candidate construction covers interfaces before
spending its budget on additional cross-interface combinations.
Enrollment advertises strategy support. A plan waits for at least one confirmed
socket in every mutually enabled, requested lane on each side. Independently
arriving registrations therefore cannot accidentally produce a partial plan.

Both clients first install an `Offer` and acknowledge `Ready` through Æthernet.
Only after both acknowledgements does rendezvous send `Go` to both clients.
The start delay is relative to reception, with repeated UDP flights to tolerate
signaling asymmetry; synchronized wall clocks are unnecessary.

The standard strategy uses the regular socket on each interface. One peer
initiates the existing authenticated handshake while the other sends NAT-opening
packets simultaneously. Both local and external destination candidates are tried.

The AeroAdmin-style strategy uses two independent socket pairs per interface
combination. The first pair has A as initiator and B as responder; the second
reverses the roles. Each pair has its own session token, ephemeral exchange,
directional keys, sequence counters and health. The same Ready/Go barrier starts
both pairs. Either working pair is usable; both can remain available for RTT/loss
selection. The example requires both pairs to authenticate when testing this mode.

NAT-opening packets use native peer method ID 9. They disclose AppId/profile and
an opaque planned session token, not UID. Dedicated traversal sockets reject
unsolicited handshakes without a corresponding plan. A planned handshake can
learn a peer-reflexive source endpoint. The selected endpoint becomes fixed after
mutual authentication. The existing certificate/expected-UID verification and
two-sided UDP key confirmation are required before the path is published as ready.
Rendezvous cannot turn an unconfirmed candidate into an online peer.

## Probes, NAT state and recovery

Every retained active path uses the existing `ae::Ping` lifecycle, user RX timing
and shared `ReceivePresenceState`, using its data socket and exact peer endpoint.
Probes measure RTT, jitter and loss and refresh that NAT mapping. There are no
independent one-second/four-second timers. The client API reports simultaneous
cloud/LAN/direct-IP/hole-punch reachability and keeps the authoritative cloud
observation separate. See [client_presence.md](client_presence.md).

With backups enabled all established alternatives are probed. With backups
disabled, only the preferred outgoing path is actively maintained; incoming
probes on a reciprocal session are still answered so the other side can keep
its independently selected outgoing path. Healthy LAN/direct IP also suppresses
unnecessary new traversal attempts when backups are disabled.

Confirmed rendezvous registration follows the same user timing for the
rendezvous UID. Its encrypted challenge echoes RX timing and offline timeout;
freshness uses the shared confirmed schedule. Missing registration retries every
second and enrollment every two seconds during setup. Inactive credentials last
at least 180 seconds, extended by longer configured timing. Pending peer requests retry
every three seconds, offers/Go every 500 ms, and a coordination job lasts nine
seconds. A native handshake attempt has the existing five-second lifetime.
Native sockets reserve the plan before acknowledging Ready. A remote Hello that
arrives before the local Go is therefore attributed to its planned hole-punch
route rather than an unsolicited direct-IP session.

The broker bounds registrations (512 globally, 64 per UID), pending requests and
jobs (128 each), and candidate pairs (10 per lane, at most 30 per plan). Native
transport bounds simultaneous hole-punch sessions at 32, preserving room for
LAN/direct candidates within its overall 64-path capacity. These are resource
bounds; arbitrary interface/peer counts are not unboundedly expanded.
Plans interleave the lanes so one strategy cannot consume every remaining slot.
Under pressure, an unanswered attempt older than two seconds for another peer
may be replaced; authenticated paths and fresh attempts are preserved.

The rendezvous control stream contributes to cloud transport accounting under
its reserved logical ID. Per-peer hole-punch statistics describe peer data paths,
their handshake and Ping/Pong traffic; they exclude UDP registration to rendezvous.

If rendezvous is offline or NAT traversal fails, enabled cloud delivery remains
available. Hole punching cannot guarantee a direct connection for every NAT or
firewall combination. Endpoint-dependent mappings can prevent it; two socket
pairs do not provide a universal bypass. See the terminology and traversal
discussion in [RFC 5128](https://www.rfc-editor.org/rfc/rfc5128) and
[RFC 4787](https://www.rfc-editor.org/rfc/rfc4787).

## Public-host example

Build with `AE_BUILD_EXAMPLES=ON` and `AE_FILTRATION=ON`. Start from separate
working directories for each persistent client and each crypto profile.

```text
peer-rendezvous serve 49152 0
peer-rendezvous peer RV_UID RV_IP RV_PORT both - 180
peer-rendezvous peer RV_UID RV_IP RV_PORT both OTHER_PEER_UID 35
```

`serve` starts an ordinary Æthernet client plus the UDP broker, writes its public
UID/port/profile to `rendezvous.info`, and runs indefinitely when duration is zero.
It checkpoints persistent client state every five seconds and on SIGINT/SIGTERM.
The passive demo peer prints its UID to `peer.info`. The active peer first sends
a cloud message, waits for every requested UDP lane, then verifies 20 direct
messages without increasing its cloud-payload counter. `standard` and `dual`
select the strategies separately; `fallback` verifies cloud echo without waiting
for traversal. The example echo service is a test application, not storage.

Clients must enforce application access rules; a certified UID is an identity,
not permission to use a storage or remote-control service. AppId is a namespace,
not a password. The same passive-UID-privacy and trust limitations as
`peer_protocol.md` apply. No independent cryptographic audit is claimed.

The public-host run also exposed an existing poller/packet-queue mutex inversion
in cloud TCP. Write-ready callbacks now coalesce a send task through the scheduler
instead of acquiring the queue mutex while holding the descriptor mutex. UDP uses
the same notification path. Regression tests verify that notifications perform no
socket work on the producer thread and only send when the scheduler runs.

Repeated new peers also exposed a missing completion in cloud configuration
lookup: successful requests stayed in the five-slot action pool. Requests now
finish after either result, ignore subsequent updates, and release their slot.
The regression exceeds the pool capacity with both successful and failed results.
Concurrent lookup exhaustion is reported through `TryGetCloud`; P2P streams
retain their buffered messages and retry after 100 ms when a slot is available.

Offline storage is unchanged by this feature.
