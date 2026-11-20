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
