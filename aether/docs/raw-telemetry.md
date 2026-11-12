# Raw telemetry integration

`AetherTeleSink` records compact raw data in the `aether` namespace. The object
system uses its own sink and the `objects` namespace. Their writers, runtime
filters, tag indices and statistics are independent, even though Aether binds
both sinks to one `TeleStatisticsBackend` for persistence. Application sinks can
use another identity and backend.

## Build requirements

CMake uses Python 3 to generate the complete telemetry callsite catalog.

Raw telemetry requires C++ exceptions to contain callback failures, and Aether
uses RTTI when adopting persistent telemetry backends. The CMake target exports
these compiler settings to its consumers. The provided optimization toolchains
and ESP-IDF `sdkconfig.defaults` enable both features; existing ESP-IDF build
configurations must enable `CONFIG_COMPILER_CXX_EXCEPTIONS` and
`CONFIG_COMPILER_CXX_RTTI` as well.

## Startup and restart

The bootstrap backend accepts events before the object domain exists. After
`TeleStatistics` loads, startup transfers the bootstrap archive once and redirects
late callbacks to the persistent backend. Loading imports already accepted metric
totals as the baseline; those totals do not become new outgoing metric deltas.

Use stable writer IDs when a particular worker's statistics must continue across
restarts. A live writer ID has one owner thread. Synchronized sink checkpoints
can drain its stream while recording and scope completion use the same writer
lock. A raw `Stream` used directly still requires one owner.

Each namespace stores its tag catalog identity beside its raw blocks. Aether's
catalog is generated with the build's source map. When that identity changes,
loading preserves the old namespace as `aether@<old-catalog>` and starts a fresh
current namespace. Old numeric indices are never interpreted as new tags.
The object system has explicit stable IDs and its own catalog identity.

Restart tests must use filtration (`AE_FILTRATION=ON`) or production loading.
Distillation with filtration disabled intentionally clears persisted state and
creates a fresh domain on every application construction.

## Drain and durable storage

`IBackend::Append` accepts a complete immutable block or rejects all its bytes.
`TeleStatisticsBackend` validates and appends that block atomically under its
mutex. Rejection leaves the stream's exact pending bytes available for retry.
Metric batches contain newly accepted deltas, independently of the restored
baseline. A snapshot contains only complete blocks accepted by the backend.

For this backend, successful drain means **accepted in RAM**. It does not mean
written or synced to disk. `DrainAetherTelemetry()` flushes logs and metric
deltas from both sinks into that archive and reports failure. The established
domain save path then serializes the archive as part of `TeleStatistics`.
Runtime streams, callbacks, mutexes and shared ownership do not enter the object
graph. Archive loading stages and validates the entire value before publishing
it; the byte journal is serialized in bounded chunks.

Drain callbacks must not call domain save. They may run on a worker thread, and
domain saving itself emits object telemetry. Checkpoint on the domain's owner
thread: stop new work, finish scopes/join workers, drain, then save. A caller that
must observe storage errors should inspect the result of `DomainGraph::SaveRoot`;
the older `ObjPtr::Save()` convenience API returns `void`. The filesystem writer
commits a complete serialized object through a temporary file and replacement;
custom storage implementations define their own durability guarantees. A RAM
backend cannot recover data that was never included in a successful durable save.

The default backend retains accepted history in RAM without an automatic size
limit or eviction. Domain checkpoints and uploads do not consume it. An explicit
backend capacity rejects complete appends and preserves retries; deployments
requiring bounded long-term memory need an external journal or a retention policy.

Application shutdown closes recording before its final checkpoint. No live
telemetry scope or worker should outlive its `AetherApp`: scope completion after
the last durable save cannot be recovered on restart. Start, stop, pause and
resume are ordinary timestamped events. Recovery does not require a stop event.

## Upload fragments and receiver

`MakeTelemetryPayloads(archive, max_packet_bytes)` serializes the complete archive
once, including all namespace catalogs and environments. It fragments those bytes
into the existing opaque `Telemetric::cpp.blob`. Aether sizes each actual
`LoginApi::LoginByAlias` packet, including its UID, encrypted authorized bytes and
length prefixes, against the transport's packet limit. Candidate sizing is pure
and does not encrypt, log or advance nonces. The two-argument helper limits only
the inner authorized packet; transport callers supply the size-transform overload.
A single
raw record or large environment may span messages without changing the raw
record format. No journal data is consumed by uploading.

Each fragment contains a transfer ID, total archive byte length, offset and byte
slice. These are network assembly fields, not telemetry sessions. Environment
fields in the outer `Telemetric` wrapper remain empty; the complete environment
is inside the archive and is not repeated per fragment. Deferred cloud calls own
their payload until execution.

The C++ receiver helpers are declared in `aether/ae_actions/telemetry_payload.h`:

- `DecodeTelemetryFragment(payload)` validates and exposes one fragment.
- `ReassembleTelemetryPayloads(parts)` accepts any order and identical duplicate
  fragments. It rejects missing parts, contradictory overlaps, mixed transfers,
  malformed lengths, trailing bytes and invalid archives before returning data.

The receiver groups parts by transfer ID and owns its buffering limits, expiry
and retransmission policy. An upload is a complete archive snapshot, not a new
incremental metric batch: processing repeated uploads must not sum their shared
history again. Server collection/reassembly is a separate deployment step; the
existing outer API method and opaque blob remain the transport entry point.
