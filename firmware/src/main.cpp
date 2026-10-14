#include <M5StickCPlus.h>

// M5StickCPlus' bundled eSPI header defines min/max macros. They collide with
// std::chrono::duration::min/max included by ESPAsyncWebServer.
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <WiFi.h>
#include <ESPAsyncWebServer.h>

#include "mb_rl7023_11.h"
#include "credentials.h"
#include "metrics_config.h"

namespace {
constexpr uint32_t kWifiLogIntervalMs = 10000;
}

mb_rl2023_11 power(1, 0, 26, bid, bpass);
AsyncWebServer server(80);

volatile int instantaneous_power = 0;
volatile bool instantaneous_power_valid = false;
volatile uint32_t power_sample_at = 0;
volatile uint32_t power_sample_gap_ms = 0;
volatile uint32_t power_sample_count = 0;
volatile int instantaneous_current_r_deciamps = 0;
volatile int instantaneous_current_t_deciamps = 0;
volatile bool instantaneous_current_r_valid = false;
volatile bool instantaneous_current_t_valid = false;
volatile uint32_t cumulative_energy_forward_raw = 0;
volatile uint32_t cumulative_energy_reverse_raw = 0;
volatile bool cumulative_energy_import_valid = false;
volatile bool cumulative_energy_export_valid = false;
uint32_t next_power_request_at = 0;
uint32_t next_current_request_at = 0;
uint32_t next_energy_import_request_at = 0;
uint32_t next_energy_export_request_at = 0;
uint32_t next_energy_unit_request_at = 0;
uint32_t next_wifi_log_at = 0;
bool wifi_was_connected = false;
bool power_request_started = false;
bool slow_request_since_power = false;

bool reached(uint32_t now, uint32_t target) {
    return static_cast<int32_t>(now - target) >= 0;
}

String energy_kwh(uint32_t raw) {
    return String(static_cast<float>(raw) * power.energy_unit_kwh(), 3);
}

void update_display() {
    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextDatum(3);
    M5.Lcd.setCursor(0, 40, 7);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(1);
    M5.Lcd.printf("%d", instantaneous_power);
    M5.Lcd.setCursor(M5.Lcd.getCursorX(), M5.Lcd.getCursorY() - 5, 2);
    M5.Lcd.setTextSize(4);
    M5.Lcd.printf("W");
}

void setup_http_server() {
    server.on("/metrics", HTTP_GET, [](AsyncWebServerRequest *request) {
        int power_snapshot = instantaneous_power;
        bool valid_snapshot = instantaneous_power_valid;
        const uint32_t power_sample_at_snapshot = power_sample_at;
        const uint32_t power_gap_snapshot = power_sample_gap_ms;
        const uint32_t power_count_snapshot = power_sample_count;
        int current_r_snapshot = instantaneous_current_r_deciamps;
        int current_t_snapshot = instantaneous_current_t_deciamps;
        bool current_r_valid_snapshot = instantaneous_current_r_valid;
        bool current_t_valid_snapshot = instantaneous_current_t_valid;
        uint32_t forward_energy_snapshot = cumulative_energy_forward_raw;
        uint32_t reverse_energy_snapshot = cumulative_energy_reverse_raw;
        bool energy_import_valid_snapshot = cumulative_energy_import_valid;
        bool energy_export_valid_snapshot = cumulative_energy_export_valid;

        String response;
        response.reserve(2048);
        if (metrics_config::kEnableInstantaneousPower) {
            response += "# HELP instantaneous_power Instantaneous power (in W).\n";
            response += "# TYPE instantaneous_power gauge\n";
            if (valid_snapshot) {
                response += "instantaneous_power{location=\"tsukuba\"} ";
                response += String(power_snapshot);
                response += "\n";
            } else {
                response += "# instantaneous_power is not available yet\n";
            }
            if (metrics_config::kEnablePowerTimingMetrics) {
                response += "# HELP instantaneous_power_samples_total Received power samples, including unchanged values.\n";
                response += "# TYPE instantaneous_power_samples_total counter\n";
                response += "instantaneous_power_samples_total{location=\"tsukuba\"} " + String(power_count_snapshot) + "\n";
                if (valid_snapshot) {
                    response += "# HELP instantaneous_power_sample_age_seconds Seconds since the last received power sample.\n";
                    response += "# TYPE instantaneous_power_sample_age_seconds gauge\n";
                    response += "instantaneous_power_sample_age_seconds{location=\"tsukuba\"} ";
                    response += String(static_cast<uint32_t>(millis() - power_sample_at_snapshot) / 1000.0f, 3) + "\n";
                    if (power_count_snapshot > 1) {
                        response += "# HELP instantaneous_power_sample_gap_seconds Interval between the last two received power samples.\n";
                        response += "# TYPE instantaneous_power_sample_gap_seconds gauge\n";
                        response += "instantaneous_power_sample_gap_seconds{location=\"tsukuba\"} ";
                        response += String(power_gap_snapshot / 1000.0f, 3) + "\n";
                    }
                }
            }
        }

        if (metrics_config::kEnableInstantaneousCurrent) {
            response += "# HELP instantaneous_current_amps Instantaneous current (in A).\n";
            response += "# TYPE instantaneous_current_amps gauge\n";
            if (current_r_valid_snapshot) {
                response += "instantaneous_current_amps{phase=\"r\",location=\"tsukuba\"} ";
                response += String(static_cast<float>(current_r_snapshot) / 10.0f, 1);
                response += "\n";
            }
            if (current_t_valid_snapshot) {
                response += "instantaneous_current_amps{phase=\"t\",location=\"tsukuba\"} ";
                response += String(static_cast<float>(current_t_snapshot) / 10.0f, 1);
                response += "\n";
            }
            if (!current_r_valid_snapshot && !current_t_valid_snapshot) {
                response += "# instantaneous_current_amps is not available yet\n";
            }
        }

        if (metrics_config::kEnableCumulativeEnergyImport) {
            response += "# HELP cumulative_energy_import_kwh Cumulative imported energy (in kWh).\n";
            response += "# TYPE cumulative_energy_import_kwh gauge\n";
            if (energy_import_valid_snapshot && power.has_energy_unit()) {
                response += "cumulative_energy_import_kwh{location=\"tsukuba\"} ";
                response += energy_kwh(forward_energy_snapshot);
                response += "\n";
            } else {
                response += "# cumulative_energy_import_kwh is not available yet\n";
            }
        }

        if (metrics_config::kEnableCumulativeEnergyExport) {
            response += "# HELP cumulative_energy_export_kwh Cumulative exported energy (in kWh).\n";
            response += "# TYPE cumulative_energy_export_kwh gauge\n";
            if (energy_export_valid_snapshot && power.has_energy_unit()) {
                response += "cumulative_energy_export_kwh{location=\"tsukuba\"} ";
                response += energy_kwh(reverse_energy_snapshot);
                response += "\n";
            } else {
                response += "# cumulative_energy_export_kwh is not available\n";
            }
        }

        if (metrics_config::kEnableCumulativeEnergyImport ||
            metrics_config::kEnableCumulativeEnergyExport) {
            response += "# HELP cumulative_energy_unit_kwh Energy unit multiplier (in kWh per raw count).\n";
            response += "# TYPE cumulative_energy_unit_kwh gauge\n";
            if (power.has_energy_unit()) {
                response += "cumulative_energy_unit_kwh{code=\"";
                response += String(power.energy_unit_code(), HEX);
                response += "\",location=\"tsukuba\"} ";
                response += String(power.energy_unit_kwh(), 4);
                response += "\n";
            } else {
                response += "# cumulative_energy_unit_kwh is not available yet\n";
            }
        }

        request->send(200, "text/plain; version=0.0.4", response);
    });

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String response = "please access /metrics\nRL7023: ";
        response += power.status();
        response += "\nWiFi: ";
        response += (WiFi.status() == WL_CONNECTED ? "connected" : "connecting");
        response += "\nEnergy unit: ";
        response += (power.has_energy_unit() ? String(power.energy_unit_kwh(), 4) + " kWh" : "unknown");
        response += "\n";
        request->send(200, "text/plain", response);
    });

    server.on("/healthz", HTTP_GET, [](AsyncWebServerRequest *request) {
        const bool healthy = WiFi.status() == WL_CONNECTED && power.is_connected();
        request->send(healthy ? 200 : 503, "text/plain", healthy ? "ok\n" : "not ready\n");
    });

    server.begin();
}

void setup() {
    M5.begin();
    M5.Lcd.setRotation(3);
    Serial.begin(115200);

    M5.Lcd.fillScreen(BLACK);
    M5.Lcd.setTextColor(WHITE);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(0, 0);
    M5.Lcd.println("Starting...");

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password);

    // Start both state machines. Neither one blocks the other.
    power.begin();
    setup_http_server();
}

void schedule_meter_request(uint32_t now) {
    // Service at most one slow request between overdue power requests.
    if (power.is_ready()) {
        const bool energy_enabled =
            metrics_config::kEnableCumulativeEnergyImport ||
            metrics_config::kEnableCumulativeEnergyExport;

        const bool power_due = metrics_config::kEnableInstantaneousPower &&
                               reached(now, next_power_request_at);
        const bool current_due = metrics_config::kEnableInstantaneousCurrent &&
                                 reached(now, next_current_request_at);
        const bool combine_current = metrics_config::kCombinePowerAndCurrent &&
                                     power.can_combine_current() && current_due;

        // Never put a chain of slow transactions ahead of overdue power.
        if (power_due && (!power_request_started || slow_request_since_power)) {
            // A combined E7+E8 is slower too. After a slow transaction,
            // force E7 alone before allowing another slow/combined request.
            if (power.request_instantaneous_power()) {
                power_request_started = true;
                slow_request_since_power = false;
                next_power_request_at = now + metrics_config::kInstantaneousPowerIntervalMs;
            }
        } else if (energy_enabled && !power.has_energy_unit() &&
            reached(now, next_energy_unit_request_at)) {
            if (power.request_energy_unit()) {
                slow_request_since_power = true;
                next_energy_unit_request_at =
                    now + metrics_config::kEnergyUnitRetryIntervalMs;
            }
        } else if (current_due) {
            if (power_due && combine_current && power.request_instantaneous_power(true)) {
                slow_request_since_power = false;
                next_power_request_at = now + metrics_config::kInstantaneousPowerIntervalMs;
                next_current_request_at = now + metrics_config::kInstantaneousCurrentIntervalMs;
            } else if (power.request_instantaneous_current()) {
                slow_request_since_power = true;
                next_current_request_at =
                    now + metrics_config::kInstantaneousCurrentIntervalMs;
            }
        } else if (metrics_config::kEnableCumulativeEnergyImport &&
                   reached(now, next_energy_import_request_at)) {
            if (power.request_cumulative_energy_import()) {
                slow_request_since_power = true;
                next_energy_import_request_at =
                    now + metrics_config::kCumulativeEnergyIntervalMs;
            }
        } else if (metrics_config::kEnableCumulativeEnergyExport &&
                   reached(now, next_energy_export_request_at)) {
            if (power.request_cumulative_energy_export()) {
                slow_request_since_power = true;
                next_energy_export_request_at =
                    now + metrics_config::kCumulativeEnergyIntervalMs;
            }
        } else if (metrics_config::kEnableInstantaneousPower &&
                   reached(now, next_power_request_at)) {
            if (power.request_instantaneous_power()) {
                slow_request_since_power = false;
                next_power_request_at =
                    now + metrics_config::kInstantaneousPowerIntervalMs;
            }
        }
    }
}

void loop() {
    const uint32_t now = millis();
    M5.update();

    power.loop();

    const bool wifi_connected = WiFi.status() == WL_CONNECTED;
    if (wifi_connected && !wifi_was_connected) {
        Serial.println("WiFi connected");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
    } else if (!wifi_connected && static_cast<int32_t>(now - next_wifi_log_at) >= 0) {
        Serial.println("WiFi is still connecting...");
        next_wifi_log_at = now + kWifiLogIntervalMs;
    }
    wifi_was_connected = wifi_connected;

    // Consume completed samples before starting another request: request_*()
    // clears the corresponding new-data flag, including when power is overdue.
    int measured_power = 0;
    if (metrics_config::kEnableInstantaneousPower &&
        power.take_instantaneous_power(measured_power)) {
        const uint32_t received_at = millis();
        if (instantaneous_power_valid) {
            power_sample_gap_ms = received_at - power_sample_at;
        }
        power_sample_at = received_at;
        ++power_sample_count;
        instantaneous_power = measured_power;
        instantaneous_power_valid = true;
        Serial.println(measured_power);
        update_display();
    }

    int measured_current_r = 0;
    int measured_current_t = 0;
    bool measured_current_r_valid = false;
    bool measured_current_t_valid = false;
    if (metrics_config::kEnableInstantaneousCurrent &&
        power.take_instantaneous_current(
            measured_current_r, measured_current_t,
            measured_current_r_valid, measured_current_t_valid)) {
        instantaneous_current_r_deciamps = measured_current_r;
        instantaneous_current_t_deciamps = measured_current_t;
        instantaneous_current_r_valid = measured_current_r_valid;
        instantaneous_current_t_valid = measured_current_t_valid;
        Serial.print("current R/T (0.1A): ");
        Serial.print(measured_current_r);
        Serial.print("/");
        Serial.println(measured_current_t);
    }

    uint32_t measured_energy_forward = 0;
    if (metrics_config::kEnableCumulativeEnergyImport &&
        power.take_cumulative_energy_import(measured_energy_forward)) {
        cumulative_energy_forward_raw = measured_energy_forward;
        cumulative_energy_import_valid = true;
        Serial.print("cumulative energy import raw: ");
        Serial.print(measured_energy_forward);
        Serial.println();
    }

    uint32_t measured_energy_reverse = 0;
    if (metrics_config::kEnableCumulativeEnergyExport &&
        power.take_cumulative_energy_export(measured_energy_reverse)) {
        cumulative_energy_reverse_raw = measured_energy_reverse;
        cumulative_energy_export_valid = true;
        Serial.print("cumulative energy export raw: ");
        Serial.println(measured_energy_reverse);
    }

    schedule_meter_request(millis());

    // Give the Wi-Fi task time to run without introducing a protocol wait.
    delay(1);
}
