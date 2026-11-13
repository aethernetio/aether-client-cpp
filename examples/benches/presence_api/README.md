# Real SDK presence APIs

This probe measures `Client::IsLocallyOnline()` and the asynchronous result of
`Client::QueryPeerPresence(peer_uid)` directly. It sends no self-message or
peer echo and does not infer API state from transport links. This integration
is based on main `3ac320a0`, including its streaming and adaptive datagram fixes.
Historical measurements on `bench/presence-api-streaming` used a different
binary and must not be attributed to this integration.

Native presence classification and DTOs originate in `6beb3150` (the Cursor
experiment is `f6559e87`) and PR #550 (`fc61d36c`). `QueryPeerPresence` now
queries one resolved server; the higher-level `PeerPresenceQuery` resolves the
peer cloud and coordinates the results. The wire follows the published
`io.aether:client-server-work-api:0.0.100` / `0.0.101` contract, rather than the
older server GitHub main. Method 35 `GetClientTiming` is void, with UID only
(17 bytes including method ID); its response is safe method 41
`SendClientTiming(uid, timing)` (33 bytes). There is no request ID. The old
Cursor RPC declaration prepended four bytes to UID and expected a nonexistent
result-0 reply. Both request binding and response dispatch are now corrected.
The legacy push Ack has no request ID, so the existing single-flight Ping and
late-Ack ownership are retained; the historical overlapping prefix scheduler
is not used. Local schedules use the actual send time plus the pre-send RTT
percentile divided by two, capped at the observed Ack receipt time.

Before publishing its UID, each worker waits for confirming Pongs for the
configured one-second interval/window on every selected server. It then makes
one diagnostic `GetClientTiming(self_uid)` call per selected server. These are
API reads, not echo packets, and are logged as `self_server_timing`, never as
peer presence. A decoded timing with zero next-Ping delta means no announcement
and yields UNKNOWN; a missing timing reply also yields UNKNOWN but is recorded
with `has_timing=false`. An expired announced deadline yields OFFLINE.

The query uses native CloudRequest execution-state/timeout helpers with parallel
selected servers, two retries and RTT percentile/factor soft deadlines (at least
one second), bounded by a 20-second deadline including cloud lookup. Soft misses
retain the UID-filtered push subscription and do not restream or quarantine shared streaming
connections. Parallel selected-server queries do not need delayed hedging.
Errors, missing replies and empty peer clouds return UNKNOWN. Constructor work
is deferred so subscribers can observe immediate failures.

The server echoes UID but no request sequence. Pushes are matched to the server
connection and UID; retries retain the first outstanding send-time anchor.
This protocol cannot prove per-attempt freshness across a timed-out query and
its replacement. Large-delay results must be interpreted with that limitation.

The executable uses native Ping timings of one second, a one-second offline
classification timeout and an intended query start period of one second.
Queries are single-flight: a busy tick is recorded instead of reusing a pending
action as a new measurement. Local state is polled in the application loop.
The controller and both processes use Windows QPC for measured latencies;
SDK schedule timestamps remain separate diagnostics.

Each client runs in its own working directory with a unique campaign name,
trace and private TCP proxy. A six-byte IPv4/port preamble redirects only this
process's TCP connections. The controller gates both relay directions and new
connections, preserving byte order and backpressure. It changes no firewall,
NIC, global route or server and requires no administrator access. DNS can
continue independently. This is TCP silence, not physical link/NAT changes or
kernel packet-loss/RTO simulation; UDP is outside this probe.

Run the default two-cut pilot first:

```powershell
python examples/benches/presence_api/run_campaign.py `
  --exe C:/bench/native_presence_api_bench.exe --out C:/bench/api-pilot
python examples/benches/presence_api/analyze.py C:/bench/api-pilot
```

Then request the full 16-cut matrix explicitly:

```powershell
python examples/benches/presence_api/run_campaign.py `
  --exe C:/bench/native_presence_api_bench.exe --out C:/bench/api-matrix `
  --baseline 60 --off 3 10 30 60 --repeats 2 `
  --recovery 20 --recovery-budget 90 --stable-recovery 5
```

An output directory must be new. Startup allows 150 seconds by default. Before
any fault, both actual local APIs must be true and the last two distinct query
outcomes on each side must be successful ONLINE results no older than three
seconds. UNKNOWN, errors and missed callbacks invalidate this condition. A
startup failure records no cuts. Once started, cut durations are fixed and
independent of detection. The controller checks owned processes/memory during
every phase, with a default 256 MiB sampled working-set limit per worker. A
memory-limit breach immediately kills only the owned live pair before reaping;
other failures use cooperative shutdown first.
Recovery requires both observers to meet the same API health condition
continuously for five seconds. Failed trials remain censored; only the owned
pair is restarted before continuing. There is no forced Restream, P2P rebuild
or idle-loop watchdog in this benchmark.

CSV schema is `event,mono_us,a,b,c`. Core observations are:

| Event | a | b | c |
|---|---|---|---|
| `local_state` | actual API bool | unused | unused |
| `remote_query_start` | query ID | unused | unused |
| `remote_result` | query ID | ONLINE=1, OFFLINE=0, UNKNOWN=-1 | duration µs |
| `remote_sdk_state` | query ID | raw SDK enum 0/1/2 | unused |
| `remote_error` | query ID | error code | duration µs |
| `remote_unobserved` | query ID | unused | duration µs |
| `remote_query_busy` | pending query ID | age µs | unused |
| `remote_overdue` | pending query ID | age µs | unused |
| `remote_sample` | query ID | server ID | SDK status 0/1/2/3 |
| `remote_sample_timing` | server ID | next-ping delta ms | has-timing flag |

The underlying SDK enum is ONLINE=0/OFFLINE=1/UNKNOWN=2; `remote_result`
explicitly normalizes this to the table above. Per-server sample status keeps
the raw SDK encoding, including EXCLUDED=3. Authority, queried-cloud and
fallback events preserve which peer cloud was queried. A query API error is
not OFFLINE. Neither is a successful UNKNOWN result or an overdue/missed
callback. In this SDK, peer-cloud resolution failure may return successful
UNKNOWN rather than an error. Local presence uses OR aggregation over its
selected confirmed server schedules; remote presence uses AND over usable
authoritative peer servers, excluding quarantined servers.

Analysis writes `summary.json` and `trials.csv`. Primary OFFLINE latency uses
only an actual API OFFLINE observation after the cut. ONLINE latency is
reported after an observed outage; the first positive observation after
restore is also retained separately. Query start/completion/duration and
whether a result's query began before cut/restore remain visible. The
`fresh_query_online_ms` metric additionally requires a query begun after
restore. UNKNOWN/error/unobserved/pending counts and censored observations
are retained, never replaced by zero-latency success.

Remote results older than three seconds become **stale availability** for
health and stability accounting, not fabricated API OFFLINE. This prevents a
single old ONLINE result from appearing healthy forever during a stuck query.
First stable five-second ONLINE and terminal stable ONLINE are separate;
interval-start latency needs another five seconds for confirmation. A short
cut may never produce API OFFLINE and is reported as such.

Run offline regression fixtures with:

```powershell
python -m unittest discover -s examples/benches/presence_api -p "test_*.py" -v
```

Fixtures cover direct API transitions, UNKNOWN/error/pending/missed callbacks,
stale results, late flaps, fresh distinct health observations, incremental CSV
reading, the memory guard, and startup failure without injected faults. They
open no network connections and launch no real SDK workers. Persisted `state/`
directories contain credentials and must not be exported with benchmark data.

## Live validation, 2026-11-11

The probe and all five requested native targets compiled successfully; the
13 new Python tests passed. The existing SDK presence suite passed36/37; its
zero-interval confirmed-schedule test fails, without changes to presence code.

The live pilot did not reach client selection: both workers exited2 after
registration failed, with7 bytes sent and0 received before server closure.
There were0 managed cuts and0 native API observations; latency quantiles are
empty. Ordinary SDK smokes without proxy also failed bootstrap, while a
separate working push-SDK bootstrap control passed. This does not establish
presence quality or whether method35 is supported by the deployed server.
See `reports/validation-20261111.txt`. No server/protocol shim was introduced
to turn that failed startup into a presence measurement.

When remote RPC replies are unavailable, `--local-only` explicitly gates on both
local API samples (at most three seconds old), while still running/logging real
remote queries. `config.json` and `summary.json` label this scope `local_only`.
UNKNOWN remote outcomes are not promoted to ONLINE or OFFLINE, and peer latency
cannot be reported from such a run. Startup/pre-fault/recovery health requires
five continuous seconds by default. Use a baseline of at least 120 seconds to
replace the initial five-second RTT sample in the default 100-sample window.

Set `AE_PRESENCE_API_TRACE=1` in the controller environment for metadata-only
request IDs, write status, wire method/length, and incoming API command traces.
It does not print keys or message payloads; normal SDK builds do not enable it.

UDP and precise Ping-omission validation:
- Build a separate probe with USER_CONFIG=examples/benches/presence_api/user_config_udp.h.
  It disables TCP. Run the controller with --transport udp. The private relay drops
  actual datagrams while cut, including incoming responses, without changing host
  firewall, routes or other applications. A six-byte loopback control datagram
  binds each SDK socket to a dedicated remote endpoint; cloud payload is unchanged.
- Add --ping-faults --ping-counts 1 3 to omit individual typed Ping sends and ACK
  observations while all other traffic remains alive. This is application-level
  omission, distinct from TCP silence and UDP datagram loss. Each omission is
  confirmed in stderr; an unobserved injection invalidates the campaign.
- Fault hooks are compiled only with the existing AE_BENCH_TCP_PROXY benchmark
  flag, and remain inactive without process-specific environment variables.
