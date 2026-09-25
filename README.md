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
