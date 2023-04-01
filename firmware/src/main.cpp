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
  // M5.begin();
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

    response += "# HELP environ_temp Environment temperature (in C).\n";
    response += "# TYPE environ_temp gauge\n";
    response += ("environ_temp{location=\"tsukuba\"}" + String(instantaneous_power) + "\n");
  
    request->send(200, "text", response);

  });

  server.on("/", HTTP_GET, [&](AsyncWebServerRequest *request){

    request->send(200, "text", "POKEMON");
  });

  server.begin();
}

String header;

void loop() {
  instantaneous_power = power.get_instantaneous_power();
  Serial.println(instantaneous_power);
}