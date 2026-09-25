# FireBeetle 2 ESP32-C6 + Waveshare SIM7070G

The modem uses the existing SIM7070 AT socket driver. ESP32 uses the ESP-IDF
UART driver, with scheduled nonblocking reads and queued writes. This profile
is for SIM7070G, not SIM7000 firmware.

## Wiring

| SIM7070G HAT signal | FireBeetle ESP32-C6 |
| --- | --- |
| TX | GPIO17 / RX |
| RX | GPIO16 / TX |
| DTR | GPIO8 / DC |
| PWR | GPIO14 / RES |
| GND | GND |

Use 3.3 V UART logic on the HAT, a common ground, and the HAT's specified
5 V supply. GPIO14 controls the HAT PWR input, not the ESP32 reset input.
The board profile selects UART1 at 115200 baud, 8N1, without RTS/CTS.
USB Serial/JTAG is the console; UART0 console output must not drive GPIO16/17.

PWR is active high on this HAT (its transistor pulls the module PWRKEY low).
DTR is held low to keep the AT UART awake. Initialization first probes AT for
about four seconds. If the modem responds, it is left powered on. If all probes
time out, initialization pulses PWR for 1100 ms and polls AT for readiness.
Boot uses bounded AT requests and a fixed retry count (about 25 seconds
maximum with a responsive scheduler). Cancelling initialization releases PWR.
A modem still booting longer than the initial probe window cannot be distinguished
from an off modem without a STATUS connection.

## Build

Open an exported ESP-IDF environment. Run from the repository root (PowerShell):

```powershell
$repo = (Get-Location).Path
$project = "$repo/projects/xtensa_lx6/vscode/aether-client-cpp"
$build = "$repo/out/build/firebeetle-c6-sim7070"
idf.py -C $project -B $build `
  "-DIDF_TARGET=esp32c6" `
  "-DSDKCONFIG=$build/sdkconfig" `
  "-DSDKCONFIG_DEFAULTS=$project/sdkconfig.defaults;$project/sdkconfig.firebeetle_c6" `
  "-DUSER_CONFIG=$repo/config/user_config_firebeetle_c6_sim7070.h" `
  "-DCOMPILE_EXAMPLE=cloud" `
  "-DAE_FILTRATION=ON" "-DAE_DISTILLATION=OFF" build
idf.py -C $project -B $build -p COM_PORT flash monitor
```

Replace COM_PORT with the FireBeetle USB Serial/JTAG port. The shared ESP-IDF
project supports the C6 despite its historical xtensa_lx6 directory name.
Use a separate build directory from Wi-Fi or other board configurations.

The cloud example already selects AE_EXAMPLE_MODEM. Operator, SIM PIN, APN and
radio mode are configured in examples/common/aether_construct_modem.h; its
current values are MTS / internet.mts.ru / NB-IoT. Set these to match your SIM.
Persisted modem adapter settings take precedence after loading saved state;
when changing UART or operator settings, provision fresh state or update the
saved adapter explicitly. Filtration creates missing objects and retains state
for subsequent boots.

## Verification

Desktop tests compile the real ESP32 UART implementation and boot senders against
an IDF API test double. They cover partial writes, callback-driven closure,
invalid/occupied UARTs, overflow, warm/cold boot, timeout and cancellation.

```powershell
cmake --build <desktop-test-build> --target test-serial-port test-esp32-serial-port
ctest --test-dir <desktop-test-build> -R '^(test-serial-port|test-modem-shutdown|test-esp32-serial-port)$' --output-on-failure
```

These tests do not verify electrical timing, RF registration or cloud exchange
on hardware. Check those on the connected board after flashing.

References: [Waveshare SIM7070G HAT](https://www.waveshare.com/wiki/SIM7070G_Cat-M/NB-IoT/GPRS_HAT),
[ESP-IDF UART API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32c6/api-reference/peripherals/uart.html).

