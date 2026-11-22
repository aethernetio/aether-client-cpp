# Aether telemetry keeper

This is an ordinary Aether client, not the cloud server telemetry RPC. Build with
`AE_FILTRATION=ON`, `AE_BUILD_TELEMETRY_KEEPER=ON` and the matching local telemetry
checkout. Start with an explicit identity owner:

```
aether-telemetry-keeper --profile keeper-profile --parent-uid YOUR-PARENT-UID
```

The existing profile is loaded on restart; a missing named client goes through
normal Aether registration. `KEEPER_UID=...` is the recipient configured in both
products. `--storage` selects the artifact destination. The default is
`<profile>/received/<product>/<authenticated-source-uid>/<immutable-batch-id>.aetlog`.
`--run-seconds` supports a bounded live smoke test.

Delivery uses the reserved ordinary P2pStream port `0x54454c45`, with LAN,
direct-IP and hole-punching routes disabled, so frames traverse Aether cloud
relay infrastructure. An SDK write success is not a durable application ACK.
The receiver sends its own ACK only after the entire immutable artifact has
been written, synced and committed without replacement. Identical retransmits
are ACKed, conflicting batch IDs are refused. Pending partial transfers can be
resent after either process restarts.

AETLOG01 artifacts carry a build manifest and separate objects/aether/apptraverse/
product namespace blobs. The keeper preserves exact received bytes; the offline
product decoder validates the full JSON schema, map hashes and raw hashes.
Live cloud acceptance requires a real parent UID/profile and is not implied by
the deterministic receiver/outbox tests.

The sender keeps at most 64 pending batches and 16 MiB of immutable artifacts.
Its accepted mutable journal is bounded separately and applies backpressure;
acknowledged log prefixes are replaced by metric checkpoints. Compatible builds
retain separate source manifests for up to 64 unshipped build epochs. If a 65th
unshipped build must be represented, mutable telemetry is reset, with the reset
flag set; existing immutable outbox artifacts and ordinary application objects
are retained. Incompatible schemas reset the previous telemetry state before
decoding it.
