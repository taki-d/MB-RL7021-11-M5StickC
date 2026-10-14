# MB-RL7021-11-M5StickC

Firmware backup and build troubleshooting notes are in
[`docs/firmware-dump-and-build-notes.md`](docs/firmware-dump-and-build-notes.md).

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

- `/metrics` — Prometheus-compatible power, current, and cumulative-energy metrics
- `/healthz` — `200` when Wi-Fi and the meter session are connected, including response waits
- `/` — short status summary

Enable or disable each metric and adjust its polling interval in
`firmware/src/metrics_config.h` and rebuild. Power has a 1-second minimum
request interval; measured sequential responses arrive about every 2–2.4 seconds
on the tested meter. Current is disabled by default to prioritize power updates;
set `kEnableInstantaneousCurrent = true` to restore its requests and metrics.
Cumulative-energy requests remain enabled every 30 minutes and take turns on
the same transport, temporarily extending that period. If current is enabled,
E7+E8 are requested together every 30 seconds (typically about 4.4 seconds on
this meter). Occasional reply delays caused 7–8.5-second sample gaps in earlier
testing; these intervals are not hard upper bounds. Slow energy requests are
interleaved with power instead of queued in a batch. `kCombinePowerAndCurrent`
can disable the combined request; a rejection or timeout automatically falls
back to separate requests for that boot.

`kEnablePowerTimingMetrics` exposes received-sample age, the latest sample gap,
and a sample counter alongside power. Timing traces and a repeatable measurement
script are documented in [the latency investigation](docs/polling-latency-2026-10-07.md).

## On-device display

The landscape 240×135 display shows large, width-adjusted watts, Wi-Fi status,
and a time-based two-minute power trend. `RX` is the age of the last received
sample; `DT` is the interval between the last two samples. `DELAY` (5 seconds)
and `STALE` (10 seconds) distinguish an old reading from a live one. Connection
progress is shown before the first sample; an unknown value is never shown as zero.

The framebuffer is reused and only changed 16-row strips are transferred, one
per loop iteration, so drawing does not block UART servicing for a full frame.
See [display details and tests](docs/display-improvements-2026-10-07.md).

## Tests and continuous integration

[Firmware CI](https://github.com/taki-d/MB-RL7021-11-M5StickC/actions/workflows/ci.yml)
runs on pushes, pull requests, and manual dispatch:

- Protocol parser, transaction/fallback tests, and Python capture-window tests.
- An ESP32 firmware build with public dummy credentials, followed by display
  layout, history, rollover, and allocation-failure tests.
- Both C++ suites run with AddressSanitizer and UndefinedBehaviorSanitizer.
  Synthetic display previews are retained as PPM artifacts for seven days.

No device, real credentials, or GitHub Secrets are needed. CI never uploads to
hardware and never publishes firmware binaries or flash dumps. It does not
replace the physical meter latency and LCD checks documented above.

To run the same tests locally:

```sh
bash scripts/run_host_tests.sh core
# Install the M5 font dependency once before running the display tests:
pio pkg install -d firmware -e m5stick-c
bash scripts/run_host_tests.sh display
# Or run both suites:
bash scripts/run_host_tests.sh
```

The script supports `CXX`, `PYTHON`, and `TEST_BUILD_DIR` overrides. Output goes
to the ignored `.pio/host-tests` directory by default. Keep your local
`firmware/src/credentials.h`; the CI dummy header is only for clean CI runners.
PlatformIO Core is pinned in `tests/ci/requirements.txt`; tested platform and
library versions are pinned in `firmware/platformio.ini`. Official actions are
pinned to release commit SHAs.

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
