# Persistent product telemetry

The two products are AppTraverse App (`apptraverse-app`) and AeroAdmin Chat
(`aeroadmin-chat`). Both register independent `objects`, `aether`, `apptraverse`
and product namespaces before constructing `AetherApp`. The initial deployment
ships all four to one dedicated keeper client through ordinary Aether client
messaging. Namespace identities stay separate so routing can be split later.

## Recording and restart

`AetherTeleSink` and the product sinks accept concurrent writers and synchronized
`DrainAll()` checkpoints. A raw `Stream` itself has one owner. Bind a stable
logical writer ID when per-worker metric history must continue across process
restarts. OS thread IDs and live TLS, clocks, callbacks, locks and sockets are
runtime state and are never serialized.

An application process has one telemetry owner, one journal and one outbox.
Additional SDK contexts use `AetherAppContext{}.TelemetryOwner(false)` and emit
through that owner's backend. They do not rebind the global sinks, adopt a
second history into process telemetry, create a second uploader, or disable logging when they
stop. Construct the owner first and destroy it after its participants; business
objects remain local to their SDK domains. The context owner thread receives an
automatic writer ID, so two contexts cannot accidentally claim the same writer.

Raw data consists of self-contained thread blocks. A block identifies logs or
metrics and the number of writer sections. Each writer section carries its ID,
body length and record count once; timed log sections also carry an absolute
time base. Log records contain the tag and configured time delta, duration and
parameter length/data. There is no per-record writer ID, sequence, control byte
or extended-number flag. Metric values use
`TieredInt<uint8_t, 200, 5000, 100000000>`; tag and length codecs are recorded
separately in each exact offline map.

`TeleStatistics` remains an ordinary persistent `Obj`. Its normal `Save`/`Load`
uses `BinaryArchive` and the domain storage writer to persist the accepted raw
journal, immutable delivery outbox, capture cursors and source-build boundaries.
Startup merges the early object/SDK journal once into the loaded backend.
Restored metric totals become the baseline; they are not emitted again as new
deltas. Shutdown stops new recording, drains, and uses the ordinary domain save.

A successful drain means accepted in RAM. To make a checkpoint, finish scopes
and join workers, drain on the model/domain owner thread, then check
`DomainGraph::SaveRoot(...).IsOk()`. The older `ObjPtr::Save()` convenience API
returns void. Standard filesystem storage supports process restart after a
successful save; power-loss guarantees depend on the storage implementation.
A RAM domain cannot survive process termination. Filtration/production loading
is required: distillation without filtration deliberately recreates state.

The accepted journal is bounded to 8 MiB; a rejected complete append stays in
its writer for retry. The outbox has at most 64 immutable batches and 16 MiB of
artifact bytes. Backpressure does not prevent sending already accepted batches.
After every pending batch is acknowledged, captured log prefixes are removed,
metric totals remain as compact checkpoints, and any unsent suffix is retained.
Without a configured keeper or working network, these bounds eventually apply;
this is bounded buffering rather than an unbounded disk log.

## Compatibility and exact source identity

The persisted telemetry envelope checks its compatibility identity before
attempting to deserialize any old raw blocks or outbox. The identity includes
the product, wire codecs and semantic numeric tag/schema fingerprints. An
incompatible envelope is skipped and telemetry starts empty. Legacy class
version 0 is also skipped. Independent application objects retain their state.

A source commit or a line-number shift alone does not imply an incompatible
schema. Compatible upgrades retain totals and queued deliveries. Any unsent old
raw prefix is sealed with its original build manifest; new records use the new
build manifest. Old bytes are never relabeled with the new commit. Reassigning
numeric tags through a new frequency profile changes the schema and resets the
old telemetry state, as an intentional incompatible upgrade.

Exact offline catalogs are separate from the compatibility fingerprint. Every
artifact records repository commits, dirty flags, source digests, build/profile
identity and exact namespace map digests. Keep generated `build.json` and all
`catalogs/<sha256>.json` with each released binary. A commit alone cannot
reconstruct a dirty development binary, so the map archive is required.

## Delivery to the keeper

`TelemetryDeliverySession` sends immutable incremental artifacts through an
ordinary `P2pStream` with Aether cloud relay routing. Its dedicated port is
`0x54454c45`. This is distinct from the older cloud-server
`AuthorizedApi::SendTelemetry` RPC, which remains available for SDK consumers
without a configured product route. Configured products suppress that parallel
server upload; all four namespaces currently go to the keeper.

The owner saves the outbox through normal object serialization before the first
network write. A transport write success does not remove a batch. The keeper
reassembles fragments, rejects conflicting overlaps/IDs and only acknowledges
a complete artifact after syncing and committing its file. A retry preserves
the same batch ID and exact bytes; a keeper restart still deduplicates by
source UID and batch ID. The sender removes a batch only after its application
ACK and a successful owner save; failed persistence retains it for retry.

Build the keeper with `AE_BUILD_TELEMETRY_KEEPER=ON`, then run:

```text
aether-telemetry-keeper --profile keeper-profile --parent-uid YOUR-PARENT-UID
```

It prints `KEEPER_UID=...` after loading/registering and saving its identity.
Configure the products using `APPTRAVERSE_TELEMETRY_KEEPER_UID` and
`AEROCHAT_TELEMETRY_KEEPER_UID`. Empty configuration leaves upload disabled.
Use `--storage` to change the keeper artifact directory. Registration/live relay
verification requires a real parent UID/profile; deterministic local tests do
not imply that a cloud transfer was performed.

## Offline parsing and frequency allocation

A keeper file is `AETLOG01`, a little-endian manifest length, its JSON manifest,
and separate raw namespace ranges. The two parsers are:

```text
python aether-tele/tools/decode_apptraverse_app.py capture.aetlog --catalogs CATALOG_ARCHIVE --output records.jsonl --summary bytes.json
python aether-tele/tools/decode_aeroadmin_chat.py capture.aetlog --catalogs CATALOG_ARCHIVE --output records.jsonl --summary bytes.json
```

Each rejects the other product, chooses exact maps by digest, validates source
identity and raw hashes, and requires no network. Byte reports separate parameter
contents, record overhead, block framing and metrics. See the telemetry library's
`tools/TELEMETRY.md` for catalog generation and frozen frequency profiles.

The most frequent observed tags get the shortest available indices in the next
build. Objects retains explicit IDs 1..21, already one byte. Its frequent RAM
save success now increments a metric without formatting a log payload. Failures,
missing objects and product/model lifecycle events remain logs.
