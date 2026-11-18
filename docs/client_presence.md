# Client reachability and shared Ping/RX timing

Cloud and authenticated peer paths use the same `ae::Ping` request lifecycle and
`ReceivePresenceState` schedule. Peer transport supplies encrypted delivery and
correlated Acks; it does not impose a separate one-second Ping or four-second
offline timer. Each retained path has an independent request context and RTT
statistics, while timing comes from `ClientConnectivityPolicy`.

```cpp
auto policy = client->connectivity_policy().Load();
policy->ConfigureRxTimings().ForAllPriorities(
    ae::RxTimingConf::Every(10s).WithWindow(200ms));
policy->SetOfflineDetectionTimeout(1s);

// Optional: the same timing type, scoped to one peer UID and all its paths.
policy->ConfigurePeerRxTiming(peer_uid,
    ae::RxTimingConf::Every(30s).WithWindow(500ms));
policy->ResetPeerRxTiming(peer_uid);  // Inherit the common timing again.
```

Peer paths inherit priority-zero RX timing. `ForAllPriorities` updates that
default along with cloud priorities; `ForPriority<0>` also changes the peer
default. Per-server overrides remain scoped to that server. Per-peer overrides
are persistent configuration; sessions, Acks, deadlines and service-time entries
are runtime state and are rebuilt after loading.

## What confirms online

A matching authenticated Pong confirms exactly the interval and RX window sent
in that Ping. Changing desired timing retains the old confirmed promise until a
new matching Ack. A stale context or mismatched timing cannot confirm the new
configuration. Late Acks follow the same `Ping` lifecycle as cloud channels.

The common schedule projects the next expected opening with the observed RTT.
Presence expires at that expected opening plus the user's offline-detection
timeout and the existing bounded RTT/scheduler allowance. RX window length does
not replace the offline timeout. Incoming packets alone do not renew the
confirmed outgoing promise.

An interval of zero sends a reset Ping, clears the future presence promise after
its Ack, and stops periodic probes until timing changes. Reciprocal incoming
probes can still receive responses. Unreachable paths may retry through the
configured cadence; there is no hidden one-second keepalive.

Pending Ping and the local RX window acquire the existing policy suspend
blockers. `GetStatus()` includes the next peer service times as well as cloud
times. Unused cloud priority slots do not force a zero wake-up time for a
LAN-only client. UDP reception remains asynchronous while the application runs;
the RX policy supplies sleep/wake scheduling, not a reason to discard valid
packets delivered while awake.
All peer RX windows share one policy timer and suspend blocker. Native Acks
already received on the scheduler are completed there, without allocating a
second dispatch task. This keeps multi-interface probing within the existing
task pool rather than increasing its configured size.

With `keep_backup_channels=true`, every established alternative uses this
cadence. Disabling backups stops future active probes on nonpreferred paths,
while their incoming requests are still answered. Native data and probes use the
same socket and destination, so the configured cadence also maintains that NAT
mapping. A cadence longer than a NAT's idle lifetime may require re-punching;
the SDK does not silently shorten the user's interval.

## Client API

```cpp
auto self = client->GetPresence();
bool cloud = client->IsAetherOnline();
bool any_connection = client->IsLocallyOnline();

auto peer = client->GetPeerPresence(peer_uid);  // Read-only; no network query.
bool lan = peer.IsOnlineVia(ae::OnlineType::kLan);
bool direct = peer.IsOnlineVia(ae::OnlineType::kDirectIp);
bool punched = peer.IsOnlineVia(ae::OnlineType::kHolePunching);
bool via_cloud = peer.IsOnlineVia(ae::OnlineType::kAethernet);
```

`ClientPresence` contains `aether_online` and the `online_types` bit mask.
`IsLocallyOnline()` means at least one confirmed cloud or peer connection;
`IsAetherOnline()` retains the cloud-specific observation. The self snapshot
aggregates direct connections to all known peers; it does not imply that every
UID is reachable.

`PeerPresence` contains:

- `state`: overall reachability from this client (`Online`, `Offline`, `Unknown`).
- `online_types`: all currently supported observations, which can coexist.
- `aether_state`: the authoritative remote-cloud observation, independent of
  direct connectivity. `Unknown` means no usable server observation is known.

A working LAN path therefore produces `Online + Lan` even when this client has
no Æthernet connection. Conversely, a local Æthernet connection alone does not
prove that a particular peer is online. A cloud route requires a confirmed local
cloud presence and a fresh remote-server Online observation. That remote
observation remains available in `aether_state` even if the local cloud is down.

The synchronous getter uses live direct schedules and cached server samples.
An expired cached Online becomes Unknown. `QueryPeerPresence(uid)` refreshes the
server observation when needed and returns the same rich result. If a direct
connection already exists, or appears while the query waits for cloud access,
the query can complete from that connection without waiting for a cloud timeout.
Server Offline/Unknown never cancels a confirmed direct Online path. If a known
direct path expires and cloud access is unavailable, the peer becomes Offline;
an unobserved UID remains Unknown.

## Rendezvous registration

The encrypted registration challenge includes the same `RxTimingConf` and
offline timeout. A response must echo the active timing before the client marks
the registration observed. Confirmed registration refresh/enrollment follows
the policy timing for the rendezvous UID, rather than a fixed ten seconds.
Zero interval suppresses periodic refresh. Initial enrollment/challenge retries
and peer connection coordination retain their bounded setup retry timers.

The broker uses `ReceivePresenceState` for observed endpoint freshness. Inactive
credentials expire after the larger of 180 seconds or the confirmed timing plus
RX window and offline timeout. No UID is added to cleartext UDP records.

There is one current peer and rendezvous wire format, without version negotiation
or parallel legacy implementations. The UID certificate format and
Æthernet server APIs are unchanged.
