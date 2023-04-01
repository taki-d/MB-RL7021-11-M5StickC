#include "mb_rl7023_11.h"


mb_rl2023_11::mb_rl2023_11(
    uint8_t channel, uint8_t tx_pin, uint8_t rx_pin, String bid_, String bpass_
)
    : serial(channel)
{
    serial.begin(115200, SERIAL_8N1, rx_pin, tx_pin);
    bid = bid_;
    bpass = bpass_;
}

void mb_rl2023_11::clean_buf(){
    delay(1000);
    while(serial.available()){
        serial.read();
    }
}

int mb_rl2023_11::split(String data, char delimiter, String *dst) {
    int index = 0;

    for(int i = 0; i < data.length(); i++) {
        char tmp = data.charAt(i);
        if(tmp == delimiter) {
            index++;
        }else{
            dst[index] += tmp;
        }
    }

    return index + 1;
}

bool mb_rl2023_11::analyze_converted_ipv6_addr() {
    int timeout = 0;
    while(timeout < 200){
        String line = serial.readStringUntil('\n');
        DEBUG_PRINTLN(line);

        if(line.indexOf("FE80") > -1) {
            ipv6_addr = line;
            ipv6_addr.trim();

            clean_buf();
            return true;
        }
    }

    return false;
}

bool mb_rl2023_11::analyze_response(){
    int timeout = 0;
    while(timeout < (20000)){
        while(serial.available()){

            String line = serial.readStringUntil('\n');
            DEBUG_PRINTLN(line);

            if(line.indexOf("OK") > -1){
                return true;
            }else if(line.indexOf("FAIL") > -1) {
                return false;
            }
        }
        timeout++;
    }

    clean_buf();

    return false;
}

bool mb_rl2023_11::analyze_scan_response(){
    int timeout = 0;
    while(timeout < (3000000)){
        while(serial.available()){

            String line = serial.readStringUntil('\n');
            DEBUG_PRINTLN(line);

            if(line.indexOf("Channel:") > -1){   
                channel = line.substring(line.indexOf("Channel:") + 8);
                channel.trim();
            }else if(line.indexOf("Pan ID:") > -1) {
                pan_id = line.substring(line.indexOf("Pan ID:") + 7);
                pan_id.trim();
            }else if(line.indexOf("Addr:") > -1) {
                addr = line.substring(line.indexOf("Addr:") + 5);
                addr.trim();
            }else if(line.indexOf("EVENT 22") > -1){
                clean_buf();
                
                DEBUG_PRINTLN("EVENT22 spark")
                DEBUG_PRINTLN("Address is " + addr)

                if(addr == "NG"){
                    return false;
                }else{
                    return true;
                }
            }
        }
        timeout++;
    }

    clean_buf();

    DEBUG_PRINTLN("COMMAND FAILED");
    
    return false;
}

bool mb_rl2023_11::analyze_connection_status() {
    int timeout = 0;
    while(timeout < (3000000)){
        while(serial.available()){

            String line = serial.readStringUntil('\n');
            DEBUG_PRINTLN(line);

            if(line.indexOf("EVENT 25") > -1){
                clean_buf();
                
                DEBUG_PRINTLN("EVENT 25 spark")

                return true;
            }
        }
        timeout++;
    }

    clean_buf();

    DEBUG_PRINTLN("COMMAND FAILED");
    
    return false;
}

bool mb_rl2023_11::write_command(String command){
    serial.println(command);
    serial.flush();

    delay(10);

    /*
    for(int i = 0; i < result_length; i++) {
        while(serial.available() == 0);

        buf[0] = serial.readStringUntil('\n');
        DEBUG_PRINTLN(buf[0]);
    }
    */

    return true;
}

bool mb_rl2023_11::sendto_command(char* data, int len){

    serial.print("SKSENDTO 1 " + ipv6_addr + " 0E1A 1 0 ");
    serial.printf("%04X ", len);

    for(int i = 0; i < len; i++){
        serial.write(data[i]);
    }

    serial.print("\r\n");

    serial.flush();

    delay(10);

    return true;
}

bool mb_rl2023_11::setup() {

    // serial.printf("SKINFO\r");
    serial.printf("\r\n");
    clean_buf();

    write_command("SKRESET");
    if(!analyze_response()){
        return false;
    }

    write_command("SKINFO");
    if(!analyze_response()){
        return false;
    }

    write_command("SKSETPWD C " + bpass);
    if(!analyze_response()){
        return false;
    }

    write_command("SKSETRBID " + bid);
    if(!analyze_response()){
        return false;
    }

    bool flag = false;
    for(int duration = 4; duration < 10; duration++){
        write_command("SKSCAN 2 FFFFFFFF " + String(duration) + " 0");
        analyze_response();
        if(analyze_scan_response()){ 
            DEBUG_PRINTLN("SCAN OK"); 
            flag = true;   
            break;
        }
        DEBUG_PRINTLN("RETRY SCAN");
    }

    if(!flag){
        return false;
    }

    write_command("SKSREG S2 " + channel);
    if(!analyze_response()){
        return false;
    }

    write_command("SKSREG S3 " + pan_id);
    if(!analyze_response()){
        return false;
    }

    write_command("SKLL64 " + addr);
    if(!analyze_converted_ipv6_addr()){
        return false;
    }

    write_command("SKJOIN " + ipv6_addr);
    if(!analyze_connection_status()){
        return false;
    }

    return true;
}

int mb_rl2023_11::get_instantaneous_power(){
    char echonetLiteFrame[] = {
        0x10, 0x81, // EHD1,2
        0x00, 0x01, // TID
        0x05, 0xFF, 0x01, // SEOJ
        0x02, 0x88, 0x01, // DEOJ
        0x62, // ESV
        0x01, // OPC
        0xe7, // EPC1
        0x00, // PDC1
    };

    sendto_command(echonetLiteFrame, sizeof(echonetLiteFrame));

    int timeout = 0;
    while(timeout < (3000000)){
        while(serial.available()){

            String line = serial.readStringUntil('\n');
            DEBUG_PRINTLN(line);

            if(line.indexOf("ERXUDP") > -1){
                String buf[12];
                int len = split(line, ' ', buf);
                DEBUG_PRINT("len" + String(len));

                clean_buf();
                DEBUG_PRINTLN("ERXUDP send")

                String pw_str = buf[9].substring(buf[9].length() - 8);
                
                long pw = strtoul(pw_str.c_str(), NULL, 16);

                return pw;
            }
        }
        timeout++;
    }

    clean_buf();

    DEBUG_PRINTLN("COMMAND FAILED");
    
    return 0;
}

int mb_rl2023_11::get_running_status(){

}

