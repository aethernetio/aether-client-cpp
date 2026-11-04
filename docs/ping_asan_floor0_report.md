# Ping write-fail lifetime — AddressSanitizer (floor=0, diag OFF)

Date: 2026-11-03  
Branch: `fix/ping-push-model-ping-ack`  
SDK baseline: `5bb1a45d` (write-fail `TaskSubscription`) + CMake/test hardening in follow-up commit.

## Build

Directory: `build-ping-hydrogen-floor0-asan` (does not overwrite `build-ping-hydrogen-floor0`).

| Option | Value |
|--------|--------|
| `USER_CONFIG` | `config/user_config_hydrogen.h` |
| `AE_BENCH_PING_MIN_TIMEOUT_MS` | `0` |
| `AE_BENCH_CONNECTION_EVENT_DIAG` | `0` (default) |
| `AE_ADDRESS_SANITIZE` | `ON` |
| Configuration | **RelWithDebInfo** (MSVC C5072: ASAN requires `/Zi` — Release without debug fails `/WX`) |

Run tests from **x64 Native Tools** environment (`vcvars64.bat`) so ASAN runtime DLLs resolve.

### Instrumentation evidence (compile)

`aether/ae_actions/ping.cpp` (SDK), `CL.command.1.tlog` RelWithDebInfo:

- `/fsanitize=address` (twice on cl line)
- `/Zi` debug info

Executable link: `/INFERASANLIBS` (see LNK4075 in build log).

## Regression: write-fail deferred task

| Step | Check |
|------|--------|
| `test_WriteFailDeferredEmitsOnNextSchedulerTick` | After 1 ms: `IsPending()`, `cap.count==0`; after +1 ms: `cap.count==1`, write-fail result |
| `test_DestroyPingAfterWriteFailDeferredBeforeSchedulerTick` | After 1 ms: pending, no result; destroy `Ping`; +200 ms: still `cap.count==0`, no ASAN error |

## Sensitivity (unowned task, commit `20bb1007`)

Detached worktree `aether-client-cpp-asan-unowned` (not pushed): same CMake ASAN fix, **pre-`5bb1a45d`** `ping.cpp` (orphan `scheduler().Task` on write-fail), same destroy regression test injected locally.

Under ASAN (`artifacts/asan-unowned-run.txt`):

```
ERROR: AddressSanitizer: heap-use-after-free
#0 Ping::EmitWriteFailureFromWriteCallback ping.cpp:222
#1 lambda from Ping::Start ping.cpp:205
freed at test_DestroyPingAfterWriteFailDeferredBeforeSchedulerTick ping.reset() line 595
```

Exit code **1**. Confirms the regression hits the dangerous path; non-ASAN pass with `cap.count==0` alone was insufficient.

## Fixed SDK (`5bb1a45d` + test/CMake updates)

| Target | Result |
|--------|--------|
| `test-api-protocol-ping-push-ack` | **40 PASS** (12×2 push-ack + defer + recovery + lifecycle), exit 0, no ASAN reports |
| `test-api-protocol` | **PASS**, exit 0, no ASAN reports |

Prior **38** suite checks remain green; **+2** tests document write-fail scheduling (`WriteFailDeferred…`, strengthened destroy).

## Sanitizer findings on fixed branch

No registered AddressSanitizer errors on fixed binaries under the full ping suite and `test-api-protocol`.
