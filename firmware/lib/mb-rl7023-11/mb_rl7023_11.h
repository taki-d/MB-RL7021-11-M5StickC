#ifndef MB_RL7023_11_H
#define MB_RL7023_11_H

#include <Arduino.h>

#include "debug.h"

// Non-blocking driver for the ROHM RL7023-11 Wi-SUN module.
//
// Call begin() once, then call loop() from the application loop as often as
// possible. None of the public methods waits for UART input.
class mb_rl2023_11 {
public:
    mb_rl2023_11(
        uint8_t channel, uint8_t tx_pin, uint8_t rx_pin,
        String bid, String bpass
    );

    void begin();
    void loop();

    bool request_instantaneous_power();
    bool take_instantaneous_power(int &value);

    bool is_ready() const;
    bool is_busy() const;
    const char *status() const;

private:
    enum class State : uint8_t {
        Idle,
        WaitReset,
        WaitInfo,
        WaitPassword,
        WaitBid,
        WaitScan,
        WaitChannel,
        WaitPanId,
        WaitIpv6,
        WaitJoin,
        Ready,
        WaitPower,
        RetryWait,
    };

    HardwareSerial serial;

    String bid_;
    String bpass_;
    String channel_;
    String pan_id_;
    String addr_;
    String ipv6_addr_;
    String rx_line_;

    State state_ = State::Idle;
    uint32_t deadline_ = 0;
    uint32_t retry_at_ = 0;
    uint8_t scan_duration_ = 4;

    bool new_power_ = false;
    int instantaneous_power_ = 0;
    const char *status_ = "idle";

    void clear_input();
    void send_command(const String &command, State wait_state, uint32_t timeout_ms);
    void start_scan();
    void start_setup_retry(const char *reason);
    void handle_line(String line);
    void handle_command_ok(State wait_state);
    void handle_scan_line(const String &line);
    void handle_power_line(const String &line);
    void send_power_request();

    static bool reached(uint32_t now, uint32_t target);
};

#endif
