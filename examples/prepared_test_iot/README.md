# Prepared LTE IoT packet example for Windows

This example sends one prepared Aether temperature message through a cellular
modem connected to a Windows COM port. It supports **SIMCom SIM7070G** and
**Nordic Thingy:91 X**, using either **NB-IoT** or **LTE-M**.

## Choose your modem

| Hardware | Visual Studio configuration | Configuration header |
| --- | --- | --- |
| SIM7070G | `prepared-iot-sim7070` | `user_config_sim7070g.h` |
| Thingy:91 X | `prepared-iot-thingy91x` | `user_config_thingy91x.h` |

Both configurations build the same executable, `prepared-test-iot.exe`, in
separate directories. Each header enables exactly one modem driver. SIM7070G
uses the library's `Sim7070AtModem` driver; its existing macro is
`AE_ENABLE_SIM7070`. The former generic `user_config.h` has been renamed to
`user_config_sim7070g.h` to make the hardware selection explicit.

Thingy:91 X must run **Serial LTE Modem firmware** supporting `AT#XSOCKET`,
`AT#XCONNECT`, and `AT#XSEND`, as required by `Thingy91xAtModem`.

## Build in Visual Studio 2026 (no shell required)

### 1. Open the repository

Install the **Desktop development with C++** workload, including **C++ CMake
tools for Windows** and a Windows SDK, through Visual Studio Installer.

In Visual Studio, select **File > Open > Folder** and open the repository root:

```text
G:\projects\prj_aether\GitHub\aether-client-cpp
```

Open the root folder, not just `examples\prepared_test_iot`. This example uses
the root CMake project and its CPM dependencies. No generated `.sln` is needed.

### 2. Select the modem configuration

In the toolbar configuration dropdown (which may initially show `x64-Debug`),
select **prepared-iot-sim7070** or **prepared-iot-thingy91x**.

These entries are already provided in the root `CMakeSettings.json`. They set
`USER_CONFIG`, enable this example and tests, and use x64 MSVC with Ninja.
Wait for CMake configuration to finish in the Output window. The first
configuration may download dependencies and requires internet access.

### 3. Build the executable

Switch Solution Explorer to **CMake Targets View**. Find **prepared-test-iot**,
right-click it, and select **Build**. The executable will be created at:

```text
out\build\prepared-iot-sim7070\prepared-test-iot.exe
out\build\prepared-iot-thingy91x\prepared-test-iot.exe
```

After updating the serial-port shutdown implementation, perform a full clean
rebuild of the selected configuration: the `ISerialPort` interface has changed,
so previously compiled objects must not be reused.

If the configurations are missing, reopen the root folder and check that Visual
Studio is using `CMakeSettings.json`. If CMake Presets integration was explicitly
enabled in IDE options, disable it for this workflow and reopen the folder.
The supplied configurations use CMakeSettings, not CMakePresets.

See Microsoft's [CMake project guide](https://learn.microsoft.com/en-us/cpp/build/cmake-projects-in-visual-studio?view=msvc-170)
and [CMake settings reference](https://learn.microsoft.com/en-us/cpp/build/customize-cmake-settings?view=msvc-170).

## Run and debug in Visual Studio

1. Select `prepared-test-iot.exe` in the **Startup Item** dropdown.
2. Select **Debug > Debug and Launch Settings for prepared-test-iot** (the
   label may include `.exe`). Visual Studio opens `.vs/launch.vs.json`.
3. Keep the generated `project`, `projectTarget`, and `type` fields. Add or
   update `args` and `currentDir` in that target's configuration:

```json
"args": ["--help"],
"currentDir": "${cmake.buildRoot}"
```

4. Press **F5** to build and debug. `--help` prints usage without opening a
   COM port or contacting the network.
5. To send a packet, replace `args` with your modem and recipient settings:

```json
"args": [
  "COM9",
  "internet.mts.ru",
  "REPLACE_WITH_REAL_DESTINATION_UID",
  "2500",
  "nb-iot",
  "25001"
],
"currentDir": "${cmake.buildRoot}"
```

The UID placeholder must be replaced with an existing recipient's Aether UID
in UUID format. COM9, the APN, and operator code are examples; use the values
for your hardware and SIM. Select the corresponding build configuration before
switching modems, and update the COM port in the launch arguments.

The working directory setting keeps each configuration's `state` directory
inside its build directory. See Microsoft's [CMake debugging guide](https://learn.microsoft.com/en-us/cpp/build/configure-cmake-debugging-sessions?view=msvc-170)
for launch settings.

## Program arguments

```text
prepared-test-iot COM APN DESTINATION_UID [TEMPERATURE_CENTI_C] [nb-iot|lte-m] [OPERATOR_CODE] [PARENT_UID]
```

| Argument | Meaning / default |
| --- | --- |
| `COM` | Required Windows port, for example `COM9` or `COM15`. |
| `APN` | Required packet-data APN supplied by your carrier. |
| `DESTINATION_UID` | Required Aether recipient UID. |
| `TEMPERATURE_CENTI_C` | Temperature in hundredths of a degree Celsius; default `2500` (25 C), range `-3000` to `12500`. |
| Radio mode | `nb-iot` (default) or `lte-m`. |
| `OPERATOR_CODE` | Optional five- or six-digit carrier code; omitted or empty means automatic selection. |
| `PARENT_UID` | Optional registration parent; defaults to `3ac93165-3d37-4970-87a6-fa4ee27744e4`, as in the existing cloud examples. |

Arguments are positional. To specify a parent UID with automatic operator
selection, use an empty string for the operator argument in the JSON array.

The serial speed is **115200 baud**. Disable the SIM PIN before using this
example. It uses APN settings without authentication; see `ParseOptions()` in
`main.cpp` to customize `ModemInit` for other requirements. The modem needs
power, an antenna, and a SIM/carrier supporting the selected radio mode and UDP.

## What happens during a run

1. The full Aether client registers a sender through `ModemAdapter` and looks
   up the destination cloud through the cellular connection.
2. It shuts down ordinary connections, explicitly releases the serial port,
   and reserves one nonce with `PrepareSendMessageBlock`, then saves state and
   releases the preparation application.
3. A separate RAM-only context supplies the scheduler and Windows poller,
   with an empty adapter registry and no registered clients. A fresh modem
   driver connects and opens a UDP socket to the prepared IPv4 endpoint.
4. `EncodePacket` produces the packet bytes, which are passed directly to
   `IModemDriver::WritePacket` without additional Aether or TCP framing.
5. The program waits for the send result, closes sockets, and shuts down the
   modem network service and serial port through `Stop()`.

`Stop()` releases the COM port before reporting completion, including on
shutdown errors. It does not rely on destruction of the persistent object graph.
Windows waits for canceled serial reads to finish before closing the handle.
No delay is needed between the two stages.

For an optional hardware check, run
`tests/run/test-serial-port.exe --serial-reopen COM9` from the build directory.
It checks 100 immediate close/reopen cycles without sending AT commands; use
the COM port assigned to your modem and close any serial terminal first.

This demonstrates **prepare followed by send in one process**. Each run creates
a new sender in distillation mode. Full-client state is saved through the
standard application path under the working directory's `state` folder. The
prepared block stays in memory, is used once, and is not saved for reuse after
restarting the program.

The implementation follows the [thermometer prepared-send example](https://github.com/aethernetio/temperature-sensor/blob/thermometer-prepared-send-v0/main/prepared_send/prepared_send.cpp).
It preserves that example's temperature payload:

```text
03 03 0A <temperature_lo> <temperature_hi>
temperature = (centi_celsius / 100 + 30) * 3
```

Division is integer division. For 2500 (25 C), the payload is `03 03 0A A5 00`.

## Results and tests

| Exit code | Meaning |
| --- | --- |
| `0` | The modem accepted the entire packet and completed shutdown. |
| `1` | Preparation, network, encoding, send, or shutdown failed. |
| `2` | Invalid command-line arguments. |

Modem acceptance is **not recipient delivery confirmation**. Check receipt at
the destination. Real LTE testing requires connected hardware and a valid
recipient UID.

Both configurations enable tests. In Visual Studio's CMake Targets View, build
`test-prepared-packet` and `test-serial-port` as well as `prepared-test-iot`.
These cover prepared-packet encoding and modem AT operations using mock serial
ports; the example also registers help and invalid-argument CTest checks.

## Optional command-line workflow

For automation, open Developer PowerShell for VS at the repository root:

```powershell
cmake -S . -B out/build/prepared-iot-sim7070 -G Ninja `
  '-DCMAKE_BUILD_TYPE=Debug' '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON' `
  '-DAE_BUILD_PREPARED_TEST_IOT=ON' '-DAE_BUILD_EXAMPLES=OFF' `
  '-DAE_BUILD_TOOLS=OFF' '-DAE_INSTALL=OFF' '-DAE_BUILD_TESTS=ON' `
  '-DAE_DISTILLATION=ON' '-DAE_FILTRATION=OFF' `
  '-DUSER_CONFIG=examples/prepared_test_iot/user_config_sim7070g.h'
cmake --build out/build/prepared-iot-sim7070 --target prepared-test-iot test-prepared-packet test-serial-port --parallel
ctest --test-dir out/build/prepared-iot-sim7070 -R 'prepared|serial-port|modem-shutdown' --output-on-failure
```

For Thingy:91 X, change the build directory to `out/build/prepared-iot-thingy91x`
and `USER_CONFIG` to `examples/prepared_test_iot/user_config_thingy91x.h`.
An existing build using the old `user_config.h` must be reconfigured with the
renamed header before building. `CPM_SOURCE_CACHE` can share dependency downloads
between build directories. The example is included only when
`AE_BUILD_PREPARED_TEST_IOT=ON`.
