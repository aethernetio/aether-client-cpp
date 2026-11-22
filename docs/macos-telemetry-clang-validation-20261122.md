# Telemetry strict-Clang portability — 2026-11-22

Base 10f1df61d7f20c89a11feb3612f16410264abc1f, Intel macOS 13.7.8,
LLVM 20. Fill every TelemetryPersistentState aggregate field explicitly and use
span subranges for unsigned block/frame offsets. Serialization fields and values,
wire format and persistent version remain unchanged.

The production SDK objects compiled and linked in a macOS MinSizeRel consumer.
Existing Unity telemetry-state 13/13 and telemetry-persistence 15/15 passed.
These suites used the ordinary SDK catalog adapter (unchanged aether_tele.cpp,
without AE_PRODUCT_TELEMETRY_CATALOG), with the actual production serialization
and compaction objects. The external logging fixture is absent from the consumer's
strict product catalog; the initial test-harness failure with that catalog is
preserved and is not represented as product-catalog end-to-end PASS. No assertions
were disabled or mocked. New network/cloud, device and other-platform acceptance
was not run for this diagnostic-only portability change.
