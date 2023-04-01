#ifndef _MB_RL2023_11
#define _MB_RL2023_11

#include <Arduino.h>
#include <array>

#include "debug.h"

class mb_rl2023_11 {
public:
    mb_rl2023_11(
        uint8_t channel, uint8_t tx_pin, uint8_t rx_pin,
        String bid, String bpass    
    );
    bool setup();
    void clean_buf();

    bool write_command(String comand);
    bool sendto_command(char* data, int len);

    bool analyze_response();
    bool analyze_scan_response();
    bool analyze_converted_ipv6_addr();
    bool analyze_connection_status();

    int get_running_status();
    int get_instantaneous_power();
    int split(String data, char delimiter, String *dst);

private:
    HardwareSerial serial;

    String bid = "";
    String bpass = "";  
    String channel = "";
    String pan_id = "";
    String addr = "NG";  
    String ipv6_addr = "";
};


#endif