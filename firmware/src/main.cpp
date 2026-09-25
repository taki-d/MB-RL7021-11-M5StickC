#include <M5StickCPlus.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

#include "mb_rl7023_11.h"
#include "credentials.h"

namespace {
constexpr uint32_t kPowerIntervalMs = 5000;
constexpr uint32_t kWifiLogIntervalMs = 10000;
}

mb_rl2023_11 power(1, 0, 26, bid, bpass);
AsyncWebServer server(80);

volatile int instantaneous_power = 0;
volatile bool instantaneous_power_valid = false;
uint32_t next_power_request_at = 0;
uint32_t next_wifi_log_at = 0;
bool wifi_was_connected = false;

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

        String response;
        response.reserve(256);
        response += "# HELP instantaneous_power Instantaneous power (in W).\n";
        response += "# TYPE instantaneous_power gauge\n";
        if (valid_snapshot) {
            response += "instantaneous_power{location=\"tsukuba\"} ";
            response += String(power_snapshot);
            response += "\n";
        } else {
            response += "# instantaneous_power is not available yet\n";
        }

        request->send(200, "text/plain; version=0.0.4", response);
    });

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String response = "please access /metrics\nRL7023: ";
        response += power.status();
        response += "\nWiFi: ";
        response += (WiFi.status() == WL_CONNECTED ? "connected" : "connecting");
        response += "\n";
        request->send(200, "text/plain", response);
    });

    server.on("/healthz", HTTP_GET, [](AsyncWebServerRequest *request) {
        const bool healthy = WiFi.status() == WL_CONNECTED && power.is_ready();
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

    if (power.is_ready() && static_cast<int32_t>(now - next_power_request_at) >= 0) {
        if (power.request_instantaneous_power()) {
            next_power_request_at = now + kPowerIntervalMs;
        }
    }

    int measured_power = 0;
    if (power.take_instantaneous_power(measured_power)) {
        instantaneous_power = measured_power;
        instantaneous_power_valid = true;
        Serial.println(measured_power);
        update_display();
    }

    // Give the Wi-Fi task time to run without introducing a protocol wait.
    delay(1);
}
