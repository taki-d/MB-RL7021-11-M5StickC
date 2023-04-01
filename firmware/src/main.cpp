#include <M5StickCPlus.h>
#include <WiFi.h>

#include "mb_rl7023_11.h"
#include "credentials.h"

mb_rl2023_11 power(1, 0, 26, bid, bpass);

void setup() {
  // put your setup code here, to run once:
  M5.begin();
  Serial.begin(115200);

  for(int i = 0; i < 3; i++){
    if(!power.setup()){
      DEBUG_PRINTLN("FAILED");
    } else{
      break;
    }
  }
 
}

void loop() {
  Serial.println(power.get_instantaneous_power());
}