# Ping timeout → soft quarantine → late Pong recovery

## Timeout (Error `{2}`) — production path

1. `Ping::PingResponseTimeout` — `kTimedOut`, keeps `wait_result_sub_`, emits `Error{2}` (`ping.cpp`).
2. `ServerPing::OnPingResult` — **does not** call `Restream`; schedules `HandlePingTimeoutOnScheduler` on the task scheduler (not on the timeout callback stack).
3. `HandlePingTimeoutOnScheduler` → `CloudServerConnections::QuarantineForPingTimeout`:
   - Removes server from `selected_servers_`, moves to quarantined suffix.
   - `CloudQuarantineReason::kPingTimeoutWait`.
   - **Does not** `UnsubscribeFromServerState` — inbound decode and `ping_ack_event` stay active.
   - Schedules `CompletePingTimeoutWait` after `AE_CLOUD_SERVER_QUARANTINE_TIME_MS`.
   - `ScheduleReconcileServers()` fills send slots from other candidates (no forced main switch beyond vacancy fill).
4. `PingCloudServers::ServerQuarantined` — **ignored** for `kPingTimeoutWait` (no `ServerPing::Stop()`).
5. Periodic `Start` — deferred while `IsPending()` **or** `IsAwaitingLateResponse()` (`bench_ping_server_start_defer.h`, always on).

## Late Pong

1. Inbound data → API decode → `ClientApiSafe::PingAck()` → `Ping::PingResponse` (`kTimedOut` → `LateDuration`).
2. `HandleLatePingOnScheduler` (deferred task) validates:
   - Same `client_connection` instance (`bench_client_instance_id` when diag enabled; always `ping_wait_client_instance_id_` captured at ping start).
   - `quarantine_reason == kPingTimeoutWait`.
3. `RecoverFromPingTimeout` — cancels quarantine timer, clears soft quarantine, `ScheduleReconcileServers()` (no `Disconnect`).
4. Stats recorded on **current** channel only if still present; then `ping_.reset()` and next periodic ping scheduled.

## Expiry without Pong

`CompletePingTimeoutWait` (single shot; no-op if already recovered or upgraded):

- `UnsubscribeFromServerState`, `Disconnect()`, clear quarantine, emit `server_quarantine_release_event_` (drops `ServerPing` entry).

## Transport priority

- `QuarantineServer` / `server_error` while in `kPingTimeoutWait` → `UpgradePingTimeoutToTransportQuarantine` (keeps quarantine, unsubscribes, reschedules transport release).
- Late Pong does **not** recover `kTransport`.
- Non-timeout ping errors still `ScheduleRestream()` (write fail, etc.).

## Late-wait storage bound

Upper bound: **`AE_CLOUD_SERVER_QUARANTINE_TIME_MS`** (same as existing cloud quarantine), one timer per server entry (`quarantine_sub`).

## ACK correlation limits

`PingAck()` is push-model with no request id. Correlation is **connection instance + active `Ping` object** for that server; no cross-connection recovery; no in-flight request id on the wire.
