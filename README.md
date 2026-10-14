# MB-RL7021-11-M5StickC

## Firmware configuration

Create `firmware/src/credentials.h` (it is intentionally ignored by Git) and
set the Wi-SUN and Wi-Fi credentials:

```cpp
String bid = "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";
String bpass = "XXXXXXXXXXXX";
const char *ssid = "your-wifi-ssid";
const char *password = "your-wifi-password";
```

The firmware starts Wi-Fi and the RL7023 state machine independently. UART
responses are consumed from `loop()` without blocking delays, so the HTTP
server remains responsive while scanning, joining, or retrying the module.

Endpoints:

- `/metrics` — Prometheus-compatible instantaneous power metric
- `/healthz` — `200` only when Wi-Fi and the RL7023 are ready
- `/` — short status summary

## VS Code IntelliSense

Open `MB-RL7021-11-M5StickC.code-workspace` with the PlatformIO IDE and
Microsoft C/C++ extensions enabled. The repository and firmware folder each
have `.vscode/settings.json`, so opening either folder directly also configures
C/C++ include lookup.

Generate the compilation database from the repository root in a PlatformIO
terminal:

```sh
pio run -d firmware -e m5stick-c -t compiledb
```

This installs the required PlatformIO packages and writes
`firmware/compile_commands.json`. IntelliSense reads the actual compiler,
include paths, and preprocessor definitions from that file. Regenerate it after
changing `platformio.ini` or adding source files or dependencies. The database
contains local absolute paths and is excluded from Git.

Before generation, the settings provide fallback paths for project headers and
downloaded project libraries. ESP32 framework headers require the packages and
compilation database; `credentials.h` must be created separately as described
above. If diagnostics persist after generation, run `C/C++: Reset IntelliSense
Database` from the command palette.
