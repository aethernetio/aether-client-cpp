# P2P receive during soft ping quarantine (6414f137+)

## Problem

`P2pMessageStreamManager` used `RequestPolicy::All` for `CloudEventListener` API subscriptions. Soft ping quarantine removes the server from `selected_servers_` and emits `servers_update_event_`, which **reset** receive subscriptions for that server. Inbound `ClientApiSafe::SendMessage` on the **live** connection no longer reached the P2P port.

## Fix (minimal)

1. **`p2p_message_stream_manager.cpp`** — receive listener policy is always `RequestPolicy::AllRegisteredServers{}` (linked registered servers, including soft-quarantined). **Send** routing via `CallApi` / `RequestPolicy::All` unchanged. Bench `--receive-all-cloud-servers` not required.
2. **`cloud_subscription.cpp`** — subscribe only on **linked** connections; dedupe `server_id` per `ServersUpdate`.
3. **`cloud_server_connections.cpp`** — `servers_update_event_` on first **link** and **unlink**; `SubscribeToServerState` emits when already linked (listener after connect).

## Regression

`tests/test-api-protocol/test-ping-timeout-recovery.cpp`:

- `test_P2pReceivePersistsThroughSoftPingQuarantine` — **FAIL** on parent (`All` receive policy): `rx_count` stays **0** after quarantined `SendMessage`; **PASS** after fix.
- `test_P2pReceiveSubscribesAfterLateLink` — **PASS** after fix.
- `test_P2pSubscriptionClearsAfterDisconnect` — **PASS** after fix.

Local: `test-api-protocol-ping-push-ack` (includes existing ping/quarantine suite + new tests).

**cloud_runs=0**
