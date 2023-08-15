#include <M5StickCPlus.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

#include "mb_rl7023_11.h"
#include "credentials.h"

mb_rl2023_11 power(1, 0, 26, bid, bpass);

int instantaneous_power;

AsyncWebServer server(80);

void setup() {
  // put your setup code here, to run once:
  M5.begin();
  M5.Lcd.setRotation(3);

  Serial.begin(115200);

  WiFi.begin(ssid, password);
  while(WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());


  for(int i = 0; i < 3; i++){
    if(!power.setup()){
      DEBUG_PRINTLN("FAILED");
    } else{
      break;
    }
  }

  server.on("/metrics", HTTP_GET, [&](AsyncWebServerRequest *request){
    String response;

    response += "# HELP instantaneous_power instantantaneous power (in W).\n";
    response += "# TYPE instantaneous_power gauge\n";
    response += ("instantaneous_power{location=\"tsukuba\"}" + String(instantaneous_power) + "\n");
  
    request->send(200, "text", response);

  });

  server.on("/", HTTP_GET, [&](AsyncWebServerRequest *request){

    request->send(200, "text", "please access /metrics");
  });

  server.begin();
}

String header;

void loop() {
  int pw = power.get_instantaneous_power();
  if(pw != -1){
    instantaneous_power = pw;
    Serial.println(instantaneous_power);

    // write LCD
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
}