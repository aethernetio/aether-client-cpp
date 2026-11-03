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
   - Same `ClientServerConnection*` captured at ping start (`ping_wait_connection_`) and matching `ping_epoch_` (works with `AE_BENCH_CONNECTION_EVENT_DIAG=0`).
   - `quarantine_reason == kPingTimeoutWait`.
   - RTT update only if `current_channel()` is still the captured `ping_wait_channel_` pointer (transport rebuild inside the same connection does not apply stale RTT or lift quarantine).
3. `RecoverFromPingTimeout` — cancels quarantine timer, clears soft quarantine, `ScheduleReconcileServers()` (no `Disconnect`).
4. `ping_.reset()` and side effects run on a deferred `SchedulePingFinish` task (never synchronously from `OnPingResult` / `Ping::Start` stack).

## ServerPing lifetime

- `Stop()` / destructor bump `ping_epoch_`, reset deferred task subscriptions, and clear wait bindings so stale scheduler tasks are no-ops.
- `Ping::PingResponse` and write-failure emit are deferred one scheduler turn (`ping.cpp`) so `ServerPing` does not destroy `Ping` on the `Start` stack.

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

`PingAck()` is push-model with **no wire request id** (even when bench diag tracks a local `RequestId` for logging). Correlation for soft-quarantine recovery is **`ClientServerConnection*` + channel pointer + ping epoch**, not `bench_client_instance_id`.

**Duplicate / stale ACK:** After the first ping completes and a second ping is pending (or awaiting late response), another decoded `PingAck` is delivered to the same connection’s `ping_ack_event` and is treated like a fresh ACK for whatever `Ping` state is active. There is no protocol-level rejection of an “old” ACK. Local epoch binding only guards **deferred ServerPing tasks**, not duplicate push ACKs on the wire.
