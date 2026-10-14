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

    bool request_instantaneous_power(bool include_current = false);
    bool can_combine_current() const { return combine_current_supported_; }
    bool request_instantaneous_current();
    bool request_cumulative_energy_import();
    bool request_cumulative_energy_export();
    bool request_energy_unit();
    bool take_instantaneous_power(int &value);
    bool take_instantaneous_current(
        int &r_phase_deciamps, int &t_phase_deciamps,
        bool &r_phase_valid, bool &t_phase_valid
    );
    bool take_cumulative_energy_import(uint32_t &value);
    bool take_cumulative_energy_export(uint32_t &value);

    bool is_ready() const;
    bool is_connected() const;
    bool is_busy() const;
    const char *status() const;
    bool has_energy_unit() const;
    uint8_t energy_unit_code() const;
    float energy_unit_kwh() const;

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

    enum class RequestKind : uint8_t {
        None,
        InstantaneousPower,
        InstantaneousPowerAndCurrent,
        InstantaneousCurrent,
        CumulativeEnergyImport,
        CumulativeEnergyExport,
        EnergyUnit,
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
    RequestKind request_kind_ = RequestKind::None;
    uint32_t deadline_ = 0;
    uint32_t retry_at_ = 0;
    uint8_t scan_duration_ = 4;
    uint16_t transaction_id_ = 0;
    uint32_t request_started_at_ = 0;
    uint32_t last_loop_at_ = 0;
    uint32_t max_loop_gap_ms_ = 0;
    uint8_t requested_epc_ = 0;
    bool combine_current_supported_ = true;

    bool new_power_ = false;
    int instantaneous_power_ = 0;
    bool new_current_ = false;
    int current_r_phase_deciamps_ = 0;
    int current_t_phase_deciamps_ = 0;
    bool current_r_phase_valid_ = false;
    bool current_t_phase_valid_ = false;

    bool new_cumulative_import_ = false;
    bool new_cumulative_export_ = false;
    uint32_t cumulative_forward_raw_ = 0;
    uint32_t cumulative_reverse_raw_ = 0;
    bool energy_unit_valid_ = false;
    uint8_t energy_unit_code_ = 0;

    const char *status_ = "idle";

    void clear_input();
    void send_command(const String &command, State wait_state, uint32_t timeout_ms);
    void start_scan();
    void start_setup_retry(const char *reason);
    void handle_line(String line);
    void handle_command_ok(State wait_state);
    void handle_scan_line(const String &line);
    void handle_meter_line(const String &line);
    void send_meter_request(const uint8_t *properties, size_t property_count,
                            RequestKind request_kind);

    static bool reached(uint32_t now, uint32_t target);
};

#endif
