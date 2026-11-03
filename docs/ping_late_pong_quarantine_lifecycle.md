# Ping timeout → quarantine → late Pong (code trace)

Scope: production paths in `fix/ping-push-model-ping-ack` at analysis time. Unit tests use standalone `Ping` + mock stream (no `ScheduleRestream` / cloud quarantine).

## Standalone `Ping` (`aether/ae_actions/ping.cpp`)

| Step | Object / subscription | Receive path |
|------|------------------------|--------------|
| `Start` | `wait_result_sub_` → `client_safe_api().ping_ack_event`; `timeout_sub_` scheduled | Requires linked `ClientServerConnection` |
| `PingResponseTimeout` | `state_ = kTimedOut`; **`wait_result_sub_` kept**; `timeout_sub_`/`write_sub_` cleared; emits `Error{2}` | Unchanged |
| Late `PingAck` → `PingResponse` | If `kTimedOut`: clears `wait_result_sub_`, emits `LateDuration` | Same stream/API path |

No request-id matching: push-model `ping_ack_event` is per-connection (`ClientApiSafe` on that `ClientServerConnection`).

## `ServerPing` overlay (`ping_cloud_servers.cpp`)

| Step | Notes |
|------|--------|
| `OnPingResult(Ok)` / `LateDuration` | Stats only; **no** `ScheduleRestream`; **no** quarantine change |
| `OnPingResult(Error)` incl. timeout `Error{2}` | `ScheduleRestream()` → deferred `cc->Restream()` |
| `ScheduleRestream` | Does not reset `ping_`; timed-out `Ping` may still hold `wait_result_sub_` until destroyed or late ACK |

## Restream → channel / server error (`server_connection.cpp`)

```
Restream() → ChannelError()
  → channel_stream_update_sub_.Reset()
  → channel_stream_out_data_sub_.Reset()   // inbound demux stopped
  → if full_connected_: ServerError()
       → link_state = kLinkError; server_error_.Emit()
```

## Cloud quarantine (`cloud_server_connections.cpp`)

```
server_error_event → QuarantineAndReconcile → QuarantineServer
  → UnsubscribeFromServerState (cloud state/error subs)
  → move server to quarantined suffix; server_quarantined_event_.Emit()
  → DelayedTask(ReleaseQuarantinedServer, kCloudServerQuarantineTime)
ReleaseQuarantinedServer → Disconnect() → client_connection_.reset()
```

`PingCloudServers::ServerQuarantined` → `ServerPing::Stop()` (stops periodic `Start`, does **not** `ping_.reset()`).

`ServerQuarantineReleased` → erases `ServerPing` entry (drops any remaining `ping_`).

## Periodic `Start` after timeout

- `next_ping_time_` is set when the ping starts; the waiter schedules `Start()` at that time regardless of timeout vs OK.
- `#if AE_BENCH_PING_MIN_TIMEOUT_MS > 0`: defer if `ping_->IsPending()` only — **`kTimedOut` is not pending**, so defer does not protect the timed-out instance on the next tick.
- `MakePing()` **`ping_.emplace(...)`** destroys the previous `Ping` (`~Ping` → `ResetRequestSubscriptions()`), removing the late-ACK handler even if the socket were still readable.

## First operation that prevents late Pong handling

Two mechanisms (both matter; order depends on timing):

1. **Receive path (typical first on timeout):** `ServerConnection::ChannelError()` — `server_connection.cpp` ~278–279 — `channel_stream_out_data_sub_.Reset()`. Inbound data no longer reaches API decode / `ClientApiSafe::PingAck()`.
2. **Handler loss (always before next ping cycle):** `PingCloudServers::ServerPing::MakePing()` — `ping_cloud_servers.cpp` ~130 — `ping_.emplace(...)` destroys the timed-out `Ping` and clears `wait_result_sub_`.

Quarantine / `Disconnect()` (~270 `cloud_server_connections.cpp`) is later (quarantine delay) and also removes the connection object.

## Contract gap vs desired semantics

- Fast timeout already emits `Error{2}` → restream → quarantine path; **`LateDuration` does not reverse quarantine** and is not distinguished as “ping-timeout-only” in cloud layer.
- No bounded “late pong wait” after timeout separate from channel teardown or next `ping_.emplace`.

## Minimal production follow-ups (next step only)

1. On ping timeout only: avoid treating `Error{2}` like hard channel failure for receive (defer or skip `ScheduleRestream` until late-pong window expires), **or** keep receive path open while quarantined for send.
2. Hold timed-out `Ping` (or detached late-ACK waiter) until late window elapses; block `ping_.emplace` while `state_ == kTimedOut` (extend defer beyond `IsPending()`).
3. On `LateDuration` after timeout: clear **ping-timeout** quarantine reason only (do not force main switch); record quarantine cause in `CloudServerConnections`.
