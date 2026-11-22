# Æthernet C++ SDK

The Æthernet C++ SDK connects embedded devices and native applications to Æthernet, a managed connectivity layer for secure real-time messaging over unstable or bandwidth-constrained networks.

It supports C++20 builds on Linux, macOS, Windows, and ESP32/ESP-IDF. Dependencies are resolved by CMake through CPM.cmake.

[Documentation](https://aethernet.io/documentation) · [Tutorials](https://aethernet.io/tutorial) · [Examples](https://github.com/aethernetio/aethernet-examples)

## Why Æthernet

- handshake-free application requests with authentication and encryption data carried per request;
- automatic endpoint recovery when responses are delayed;
- self-provisioning for unattended devices;
- request-response and fire-and-forget messaging;
- compact binary protocol and specialized types for constrained devices;
- support for Wi-Fi and cellular IoT deployments, including NB-IoT and LTE-M integrations.

## Requirements

- CMake 3.16 or newer;
- a C++20 compiler;
- Git, used by CPM.cmake to retrieve dependencies;
- platform networking dependencies.

The first configure can take longer because CMake downloads and patches the required third-party libraries.

## Build the SDK and included examples

```bash
git clone https://github.com/aethernetio/aether-client-cpp.git
cd aether-client-cpp

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

The root build enables tools, examples, and tests by default. To build only the library:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DAE_BUILD_TOOLS=OFF \
  -DAE_BUILD_EXAMPLES=OFF \
  -DAE_BUILD_TESTS=OFF
cmake --build build --parallel
```

## Add it to another CMake project

CPM.cmake is the dependency mechanism currently used by the SDK:

```cmake
CPMAddPackage(
  NAME aether
  GITHUB_REPOSITORY aethernetio/aether-client-cpp
  GIT_TAG main
)

target_link_libraries(your_target PRIVATE aether)
```

For production builds, pin `GIT_TAG` to a reviewed commit SHA until versioned releases are available.

## Important build options

| Option | Default for a root build | Purpose |
| --- | ---: | --- |
| `AE_BUILD_EXAMPLES` | `ON` | Build bundled examples |
| `AE_BUILD_TESTS` | `ON` | Build tests |
| `AE_BUILD_TOOLS` | `ON` | Build SDK tools |
| `AE_ENABLE_STREAM_HIERARCHY` | `ON` | Build the application stream hierarchy and `Client::streams()` |
| `AE_DISTILLATION` | `ON` | Enable distillation build mode |
| `AE_FILTRATION` | `OFF` | Enable filtration mode |
| `USER_CONFIG` | empty | Path to a user configuration header |
| `FS_INIT` | empty | Path to generated saved-state data |

Set `AE_DNS_BACKEND` in `USER_CONFIG` to choose a resolver.

| Value | Resolver |
| --- | --- |
| `AE_DNS_SYSTEM` | Default for native builds. Windows uses asynchronous Winsock when available and a background lookup on older versions. Apple uses DNS-SD. Linux and Android use background `getaddrinfo`. ESP32 uses its platform resolver. |
| `AE_DNS_CARES` | Use c-ares on supported desktop and Android builds. |
| `AE_DNS_NONE` | Use numeric endpoints. This is the SDK default for Emscripten, where browser WebSockets resolve names. |

The legacy `AE_SUPPORT_CLOUD_DNS=0` setting still disables the SDK resolver
when `AE_DNS_BACKEND` is unspecified.

Build with `-DAE_ENABLE_STREAM_HIERARCHY=OFF` for consumers that use
`P2pStream` directly. Client profiles keep their stream definitions so they
can still be used by builds with the hierarchy enabled.

## ESP32

ESP32 builds use ESP-IDF components for Wi-Fi, networking, NVS, SPIFFS, and UART. For a complete device project, start with the [ESP32 examples](https://github.com/aethernetio/aethernet-examples) or the [temperature sensor reference project](https://github.com/aethernetio/temperature-sensor).

## Tests

```bash
cmake -S . -B build -DAE_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

## Maturity

The current project version is `0.1.0`. The API and installation process may change before a stable release. Do not use benchmark or performance claims without the corresponding test methodology and raw results.

## License

Apache License 2.0. See [LICENSE](LICENSE).


## Thread-block telemetry persistence

`TeleStatistics` remains a regular persistent `Obj`. Its `Save` serializes a
snapshot of accepted telemetry blocks through `BinaryArchive` and the normal
Domain storage writer; `Load` validates the archive before publishing it.
Streams, locks, callbacks and OS thread IDs are runtime state and are not saved.
After loading, writers restore acknowledged metric totals by stable logical
writer ID. Bind background workers explicitly with `BindWriter(id)` before their
first event; keep IDs stable between runs and distinct among live writers.

For coordinated development with the new thread-block `aether-tele` checkout:

```text
cmake -S . -B build-tele -DAE_TELE_SOURCE_DIR=../aether-tele -DAE_BUILD_TESTS=ON -DAE_INSTALL=OFF -DAE_DISTILLATION=OFF -DAE_FILTRATION=ON
cmake --build build-tele --config Debug --target test-tele-persistence test-tele-process-restart
ctest --test-dir build-tele -C Debug --output-on-failure -R test-tele
```

`AE_TELE_SOURCE_DIR` is optional and uses CPM's local-source mechanism; no
telemetry sources are copied into the client. The catalog fingerprint includes
`thread_blocks` so different wire layouts are not identified as the same catalog.
Old experimental flat captures require their old decoder and are not migrated by
this integration; use compatible object state when switching wire layouts.

A normal `AetherApp` shutdown drains the sinks and invokes the ordinary object
save path. For an explicit checkpoint while the app is alive, finish/quiesce the
work being checkpointed, check `DrainAetherTelemetry()` for success, then call
`app->aether().Save()`. RAM updates alone are not durable. A kill during a write
is distinct from a restart after a successful save.

`test-tele-process-restart` starts four actual application processes in a fresh
temporary working directory. It uses `FileSystemStdStorage`, four simultaneous
workers, stable writer IDs 10..13, and changed thread creation order on every
boot. Each process adds 400 logs, verifies all previous payload identities and
per-writer metric totals, and saves through the real persistent object graph.
Normal exits check the saved object again through a fresh Domain; the middle
process exits without destructors after an explicit checkpoint/save. The final
compatible process sees all 1200 logs and matching counts without replaying
history despite different build identities. The fourth process changes the
telemetry schema and starts its telemetry with 400 logs; an independent business
object retains its counter through all four processes.

The two product integrations also persist an immutable delivery outbox in this
ordinary object. A dedicated Aether keeper client receives artifacts via cloud
relay and acknowledges them only after committing their files. Product parsers
select archived namespace maps from each artifact's exact build manifest.
See [persistent product telemetry](aether/docs/raw-telemetry.md) and the
[keeper client](tools/telemetry_keeper/README.md) for configuration and limits.
